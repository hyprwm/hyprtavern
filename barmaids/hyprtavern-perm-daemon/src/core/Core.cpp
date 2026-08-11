#include "Core.hpp"
#include "../helpers/Logger.hpp"
#include "../ui/GUI.hpp"

#include <algorithm>

#include <sys/poll.h>

constexpr const uint32_t                               TAVERN_PROTOCOL_VERSION = 1;
constexpr const uint32_t                               PD_PROTOCOL_VERSION     = 1;
constexpr const uint32_t                               MAID_PROTOCOL_VERSION   = 1;

static SP<CCHpHyprtavernCoreV1Impl>                    impl = makeShared<CCHpHyprtavernCoreV1Impl>(TAVERN_PROTOCOL_VERSION);
static SP<CHpHyprtavernBarmaidV1Impl>                  barmaidImpl;
static SP<CHpHyprtavernPermissionAuthenticationV1Impl> pdImpl;

static bool                                            hasTavernkeep(const std::vector<uint32_t>& perms) {
    return std::ranges::contains(perms, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP);
}

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

        const auto RESULT = GUI::permissionAsk(m_appName, m_appID, perm, type);

        if (!RESULT) {
            m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_UNAVAILABLE);
            return;
        }

        switch (*RESULT) {
            case GUI::PERMISSION_CHOICE_DENY: m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED); return;
            case GUI::PERMISSION_CHOICE_ALLOW_ONCE: m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_ONCE); return;
            case GUI::PERMISSION_CHOICE_ALLOW_ALWAYS: m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_PERSISTENT); return;
        }

        m_object->sendPermissionResult(perm, HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED);
    });
}

CManagerObject::CManagerObject(SP<CHpHyprtavernPermissionAuthenticationManagerV1Object>&& obj) : m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_core->removeObject(this); });

    m_perms = g_core->permDataFor(m_object->getObject()->client());

    if (!m_perms || !hasTavernkeep(m_perms->permissions)) {
        m_object->error(-1, "permission authentication protocol requires tavernkeep");
        return;
    }

    m_object->sendAvailability(GUI::available);

    m_object->setInitPermissionTransaction([this](uint32_t seq) {
        g_core->m_object.transactions.emplace_back( //
            makeShared<CTransactionObject>(         //
                makeShared<CHpHyprtavernPermissionAuthenticationTransactionV1Object>(g_core->m_object.socket->createObject(
                    m_object->getObject()->client(), m_object->getObject(), CHpHyprtavernPermissionAuthenticationTransactionV1Object::name(), seq))));
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

    // Set up the internal server before exposing anything that can dispatch new_fd.
    m_object.socket = Hyprwire::IServerSocket::open();

    if (!m_object.socket) {
        g_logger->log(LOG_ERR, "failed to create permission daemon server socket");
        return false;
    }

    pdImpl = makeShared<CHpHyprtavernPermissionAuthenticationV1Impl>(1, [this](SP<Hyprwire::IObject> obj) {
        auto x = m_object.managers.emplace_back(makeShared<CManagerObject>(makeShared<CHpHyprtavernPermissionAuthenticationManagerV1Object>(std::move(obj)))); //
    });

    barmaidImpl = makeShared<CHpHyprtavernBarmaidV1Impl>(1, [this](SP<Hyprwire::IObject> obj) {
        auto manager = makeShared<CHpHyprtavernBarmaidManagerV1Object>(std::move(obj));
        auto perms   = permDataFor(manager->getObject()->client());

        if (!perms || !hasTavernkeep(perms->permissions)) {
            manager->error(-1, "barmaid protocol requires tavernkeep");
            return;
        }

        auto x = m_object.barmaids.emplace_back(manager);

        // we're always ready
        x->sendReady();

        x->setOnDestroy([this, w = WP<CHpHyprtavernBarmaidManagerV1Object>{x}] { std::erase(m_object.barmaids, w); });
        x->setUpdateTavernEnvironment([w = WP<CHpHyprtavernBarmaidManagerV1Object>{x}](const std::vector<const char*>& names, const std::vector<const char*>& values) {
            if (names.size() != values.size()) {
                w->error(-1, "update_tavern_environment with mismatched arrays");
                return;
            }

            g_logger->log(LOG_DEBUG, "pd: updating environment with {} new values", names.size());

            bool displayEnvChanged = false;

            for (size_t i = 0; i < names.size(); ++i) {
                if (std::string_view{values[i]}.empty())
                    unsetenv(names[i]);
                else
                    setenv(names[i], values[i], true);

                const std::string_view name = names[i];
                displayEnvChanged           = displayEnvChanged || name == "DISPLAY" || name == "WAYLAND_DISPLAY";
            }

            if (displayEnvChanged)
                GUI::updateEnv();
        });
    });

    m_object.socket->addImplementation(pdImpl);
    m_object.socket->addImplementation(barmaidImpl);

    // Set up our exposed bus object only after the server can accept and filter clients.
    m_tavern.busObject = makeShared<CCHpHyprtavernBusObjectV1Object>(m_tavern.manager->sendGetBusObject("hyprtavern-perm-daemon"));

    static bool failedToExpose = false;
    failedToExpose             = false;

    m_tavern.busObject->setExposeProtocolError([](uint32_t err) { failedToExpose = true; });
    m_tavern.busObject->setNewFd([this](int fd, const char* token, const std::vector<const char*>& protocolScope) {
        auto x = m_object.socket->addClient(fd);

        if (!x) {
            g_logger->log(LOG_ERR, "failed to connect client new fd {}", fd);
            return;
        }

        std::vector<std::string> scope;
        scope.reserve(protocolScope.size());
        for (const auto& p : protocolScope) {
            scope.emplace_back(p);
        }

        x->setProtocolFilter([scope = std::move(scope)](std::string_view protocol) { return std::ranges::any_of(scope, [protocol](const auto& p) { return p == protocol; }); });

        auto permData       = permDataFor(x);
        permData->tokenUsed = token;

        if (!permData->tokenUsed.empty()) {
            // Get the permissions and trusted identity metadata before dispatching this client.
            auto response = makeShared<CCHpHyprtavernSecurityResponseV1Object>(m_tavern.manager->sendGetSecurityResponse(token));

            response->setIdentity([permData, fd](uint32_t, const char* identifier, const char*) {
                permData->appIdentifier           = identifier ? identifier : "";
                permData->appIdentifierPersistent = identifier && *identifier;
                g_logger->log(LOG_DEBUG, "incoming fd {} identifies as {}", fd, permData->appIdentifier);
            });
            response->setPermissions([permData, fd](const std::vector<uint32_t>& perms) {
                g_logger->log(LOG_DEBUG, "incoming fd {} has {} perms", fd, perms.size());
                permData->permissions = perms;
            });

            m_tavern.socket->roundtrip();
            response->sendDestroy();
            m_tavern.socket->roundtrip();
        } else
            g_logger->log(LOG_DEBUG, "incoming fd {} has no associated token", fd);
    });

    m_tavern.busObject->sendRequirePermissions({HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP});
    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_permission_authentication_v1", PD_PROTOCOL_VERSION, {HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP}, 1);
    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_barmaid_v1", MAID_PROTOCOL_VERSION, {HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP}, 0);

    m_tavern.socket->roundtrip();

    if (failedToExpose) {
        g_logger->log(LOG_ERR, "failed to expose permission authentication protocol (is another permission daemon running?)");
        return false;
    }

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

        if (fds[0].revents & POLLIN) {
            if (!m_tavern.socket->dispatchEvents()) {
                g_logger->log(LOG_ERR, "client socket fd dispatch failed");
                return;
            }
        }

        if (fds[1].revents & POLLIN) {
            if (!m_object.socket->dispatchEvents()) {
                cleanupPermData();
                g_logger->log(LOG_ERR, "server socket fd dispatch failed");
                return;
            }

            cleanupPermData();
        }

        if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "client socket fd died");
            return;
        }

        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "server socket fd died");
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

SP<SPermData> CCore::permDataFor(const SP<Hyprwire::IServerClient>& c) {
    cleanupPermData();

    if (const auto IT = m_permDatas.find(c.get()); IT != m_permDatas.end())
        return IT->second;

    auto data    = makeShared<SPermData>();
    data->client = c;
    m_permDatas.emplace(c.get(), data);
    return data;
}

void CCore::cleanupPermData() {
    std::erase_if(m_permDatas, [](const auto& e) { return !e.second || !e.second->client; });
}

void CCore::updateAvailability(bool x) {
    for (const auto& m : m_object.managers) {
        m->sendAvailability(x);
    }
}
