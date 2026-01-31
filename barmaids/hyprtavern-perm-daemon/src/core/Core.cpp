#include "Core.hpp"
#include "../helpers/Logger.hpp"
#include "../ui/GUI.hpp"

#include <sys/poll.h>

constexpr const uint32_t                               TAVERN_PROTOCOL_VERSION = 1;
constexpr const uint32_t                               PD_PROTOCOL_VERSION     = 1;
constexpr const uint32_t                               MAID_PROTOCOL_VERSION   = 1;

static SP<CCHpHyprtavernCoreV1Impl>                    impl = makeShared<CCHpHyprtavernCoreV1Impl>(TAVERN_PROTOCOL_VERSION);
static SP<CHpHyprtavernBarmaidV1Impl>                  barmaidImpl;
static SP<CHpHyprtavernPermissionAuthenticationV1Impl> pdImpl;

//

CTransactionObject::CTransactionObject(SP<CHpHyprtavernPermissionAuthenticationTransactionV1Object>&& obj) : m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_core->removeObject(this); });
    m_object->setDestroy([this]() { g_core->removeObject(this); });

    m_object->setSetAppName([this](const char* x) { m_appName = x; });
    m_object->setSetAppIdentifier([this](const char* x) { m_appID = x; });

    m_object->setAskPermission([this](uint32_t perm, hpHyprtavernPermissionAuthenticationV1AskType type) {
        if (m_inert) {
            m_object->error(-1, "ask_permission on inert object");
            return;
        }

        m_inert = true;

        const auto RESULT = GUI::permissionAsk(m_appName, m_appID);

        if (!RESULT) {
            m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_UNAVAILABLE);
            return;
        }

        if (!*RESULT) {
            m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED);
            return;
        }

        // FIXME: this is WRONG
        m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_PERSISTENT);
        return;
    });
}

CManagerObject::CManagerObject(SP<CHpHyprtavernPermissionAuthenticationManagerV1Object>&& obj) : m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_core->removeObject(this); });

    if (const auto PERM = g_core->permDataFor(m_object->getObject()->client()); PERM)
        m_perms = *PERM;

    m_object->sendAvailability(GUI::available);

    m_object->setInitPermissionTransaction([this](uint32_t seq) {
        g_core->m_object.transactions.emplace_back( //
            makeShared<CTransactionObject>(         //
                makeShared<CHpHyprtavernPermissionAuthenticationTransactionV1Object>(g_core->m_object.socket->createObject(
                    m_object->getObject()->client(), m_object->getObject(), CHpHyprtavernPermissionAuthenticationTransactionV1Object::name(), seq))));
    });
}

CManagerObject::~CManagerObject() {
    std::erase_if(g_core->m_permDatas, [this](const auto& e) {
        if (!e.client)
            return true;

        if (m_object && m_object->getObject() && m_object->getObject()->client())
            return e.client == m_object->getObject()->client();

        return false;
    });
}

void CManagerObject::sendAvailability(bool x) {
    m_object->sendAvailability(x);
}

bool CCore::init(int fd) {
    m_tavern.socket = Hyprwire::IClientSocket::open(fd);

    if (!m_tavern.socket) {
        g_logger->log(LOG_ERR, "tavern is not serving beer");
        return false;
    }

    m_tavern.socket->addImplementation(impl);

    if (!m_tavern.socket->waitForHandshake()) {
        g_logger->log(LOG_ERR, "handshake failed");
        return false;
    }

    const auto SPEC = m_tavern.socket->getSpec(impl->protocol()->specName());

    if (!SPEC) {
        g_logger->log(LOG_ERR, "protocol unsupported");
        return false;
    }

    m_tavern.manager = makeShared<CCHpHyprtavernCoreManagerV1Object>(m_tavern.socket->bindProtocol(impl->protocol(), TAVERN_PROTOCOL_VERSION));

    // set up our object

    m_tavern.busObject = makeShared<CCHpHyprtavernBusObjectV1Object>(m_tavern.manager->sendGetBusObject("hyprtavern-perm-daemon"));

    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_permission_authentication_v1", PD_PROTOCOL_VERSION, {HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP}, 1);
    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_barmaid_v1", MAID_PROTOCOL_VERSION, {HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP}, 1);

    static bool failedToExpose = false;

    m_tavern.busObject->setExposeProtocolError([](uint32_t err) { failedToExpose = true; });
    m_tavern.busObject->setNewFd([this](int fd, const char* token) {
        auto x = m_object.socket->addClient(fd);

        if (!x) {
            g_logger->log(LOG_ERR, "failed to connect client new fd {}", fd);
            return;
        }

        auto permData       = permDataFor(x);
        permData->tokenUsed = token;

        if (!permData->tokenUsed.empty()) {
            // get the perms from the bus
            auto response = makeShared<CCHpHyprtavernSecurityResponseV1Object>(m_tavern.manager->sendGetSecurityResponse(token));

            response->setPermissions([&permData, fd](const std::vector<uint32_t>& perms) {
                g_logger->log(LOG_DEBUG, "incoming fd {} has {} perms", fd, perms.size());
                permData->permissions = perms;
            });

            m_tavern.socket->roundtrip();
        } else
            g_logger->log(LOG_DEBUG, "incoming fd {} has no associated token", fd);
    });

    m_tavern.socket->roundtrip();

    if (failedToExpose) {
        g_logger->log(LOG_ERR, "failed to expose kv protocol (is a kv manager running?)");
        return false;
    }

    m_object.socket = Hyprwire::IServerSocket::open();

    pdImpl = makeShared<CHpHyprtavernPermissionAuthenticationV1Impl>(1, [this](SP<Hyprwire::IObject> obj) {
        auto x = m_object.managers.emplace_back(makeShared<CManagerObject>(makeShared<CHpHyprtavernPermissionAuthenticationManagerV1Object>(std::move(obj)))); //
    });

    barmaidImpl = makeShared<CHpHyprtavernBarmaidV1Impl>(1, [this](SP<Hyprwire::IObject> obj) {
        auto x = m_object.barmaids.emplace_back(makeShared<CHpHyprtavernBarmaidManagerV1Object>(std::move(obj)));

        // we're always ready
        x->sendReady();

        x->setOnDestroy([this, w = WP<CHpHyprtavernBarmaidManagerV1Object>{x}] { std::erase(m_object.barmaids, w); });
        x->setUpdateTavernEnvironment([w = WP<CHpHyprtavernBarmaidManagerV1Object>{x}](const std::vector<const char*>& names, const std::vector<const char*>& values) {
            if (names.size() != values.size()) {
                w->error(-1, "update_tavern_environment with mismatched arrays");
                return;
            }

            g_logger->log(LOG_DEBUG, "kv: updating environment with {} new values", names.size());

            for (size_t i = 0; i < names.size(); ++i) {
                if (std::string_view{values[i]}.empty())
                    unsetenv(names[i]);
                else
                    setenv(names[i], values[i], true);
            }
        });
    });

    m_object.socket->addImplementation(pdImpl);
    m_object.socket->addImplementation(barmaidImpl);

    g_logger->log(LOG_DEBUG, "pd: ready!");

    return true;
}

void CCore::run() {
    pollfd fds[2] = {pollfd{
                         .fd     = m_tavern.socket->extractLoopFD(),
                         .events = POLLIN,
                     },
                     pollfd{
                         .fd     = m_object.socket->extractLoopFD(),
                         .events = POLLIN,
                     }};

    while (true) {
        if (poll(fds, 2, -1) < 0) {
            g_logger->log(LOG_ERR, "poll() failed");
            return;
        }

        if (fds[0].revents & POLLIN)
            m_tavern.socket->dispatchEvents();
        if (fds[1].revents & POLLIN)
            m_object.socket->dispatchEvents();

        if (fds[0].revents & POLLHUP) {
            g_logger->log(LOG_ERR, "client socket fd died");
            return;
        }

        if (fds[1].revents & POLLHUP) {
            g_logger->log(LOG_ERR, "servur socket fd died");
            return;
        }
    }
}

void CCore::removeObject(CManagerObject* r) {
    std::erase_if(m_object.managers, [r](const auto& e) { return e.get() == r; });
}

void CCore::removeObject(CTransactionObject* r) {
    std::erase_if(m_object.transactions, [r](const auto& e) { return e.get() == r; });
}

SPermData* CCore::permDataFor(SP<Hyprwire::IServerClient> c) {
    for (auto& d : m_permDatas) {
        if (d.client != c)
            continue;

        return &d;
    }

    m_permDatas.emplace_back(SPermData{.client = c});

    return &m_permDatas.back();
}

void CCore::updateAvailability(bool x) {
    for (const auto& m : m_object.managers) {
        m->sendAvailability(x);
    }
}
