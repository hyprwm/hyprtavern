#include "Core.hpp"
#include "Kv.hpp"
#include "../helpers/Logger.hpp"

#include <filesystem>
#include <algorithm>

#include <sys/poll.h>

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/sysctl.h>
#if defined(__DragonFly__)
#include <sys/kinfo.h> // struct kinfo_proc
#elif defined(__FreeBSD__)
#include <sys/user.h> // struct kinfo_proc
#endif

#if defined(__NetBSD__)
#undef KERN_PROC
#define KERN_PROC  KERN_PROC2
#define KINFO_PROC struct kinfo_proc2
#else
#define KINFO_PROC struct kinfo_proc
#endif
#if defined(__DragonFly__)
#define KP_PPID(kp) kp.kp_ppid
#elif defined(__FreeBSD__)
#define KP_PPID(kp) kp.ki_ppid
#else
#define KP_PPID(kp) kp.p_ppid
#endif
#endif

constexpr const uint32_t              TAVERN_PROTOCOL_VERSION = 1;
constexpr const uint32_t              KV_PROTOCOL_VERSION     = 1;
constexpr const uint32_t              MAID_PROTOCOL_VERSION   = 1;

static SP<CCHpHyprtavernCoreV1Impl>   impl = makeShared<CCHpHyprtavernCoreV1Impl>(TAVERN_PROTOCOL_VERSION);
static SP<CHpHyprtavernBarmaidV1Impl> barmaidImpl;
static SP<CHpHyprtavernKvStoreV1Impl> kvImpl;

//
CManagerObject::CManagerObject(SP<CHpHyprtavernKvStoreManagerV1Object> obj) : m_object(obj) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_core->removeObject(this); });

    if (const auto PERM = g_core->permDataFor(m_object->getObject()->client()); PERM)
        m_perms = *PERM;

    m_pid = m_object->getObject()->client()->getPID();

    getAppBinary();

    m_object->setSetValue([this](const char* key, const char* val, hpHyprtavernKvStoreV1ValueType type) {
        switch (type) {
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE: {
                g_core->m_kv.setApp(m_appBinary, key, val);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_UNBOUNDED_VALUE: {
                g_core->m_kv.setGlobal(key, val);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE: {
                if (!std::ranges::contains(m_perms.permissions, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP)) {
                    m_object->error(-1, "Insufficient permissions to call set_value with tavern");
                    return;
                }

                g_core->m_kv.setTavern(key, val);
                break;
            }
        }
    });

    m_object->setGetValue([this](const char* key, hpHyprtavernKvStoreV1ValueType type) {
        switch (type) {
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE: {
                auto ret = g_core->m_kv.getApp(m_appBinary, key);
                if (!ret)
                    m_object->sendValueFailed(key, type, HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_VALUE_MISSING);
                else
                    m_object->sendValueObtained(key, ret->c_str(), type);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_UNBOUNDED_VALUE: {
                auto ret = g_core->m_kv.getGlobal(key);
                if (!ret)
                    m_object->sendValueFailed(key, type, HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_VALUE_MISSING);
                else
                    m_object->sendValueObtained(key, ret->c_str(), type);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE: {
                if (!std::ranges::contains(m_perms.permissions, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP)) {
                    m_object->error(-1, "Insufficient permissions to call set_value with tavern");
                    return;
                }

                auto ret = g_core->m_kv.getTavern(key);
                if (!ret)
                    m_object->sendValueFailed(key, type, HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_VALUE_MISSING);
                else
                    m_object->sendValueObtained(key, ret->c_str(), type);
                break;
            }
        }
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

static std::expected<std::string, std::string> binaryNameForPid(pid_t pid) {
    if (pid <= 0)
        return std::unexpected("No pid for client");

#if defined(KERN_PROC_PATHNAME)
    int mib[] = {
        CTL_KERN,
#if defined(__NetBSD__)
        KERN_PROC_ARGS,
        pid,
        KERN_PROC_PATHNAME,
#else
        KERN_PROC,
        KERN_PROC_PATHNAME,
        pid,
#endif
    };
    u_int  miblen        = sizeof(mib) / sizeof(mib[0]);
    char   exe[PATH_MAX] = "/nonexistent";
    size_t sz            = sizeof(exe);
    sysctl(mib, miblen, &exe, &sz, NULL, 0);
    std::string path = exe;
#else
    std::string path = std::format("/proc/{}/exe", sc<uint64_t>(pid));
#endif
    std::error_code ec;

    std::string     fullPath = std::filesystem::canonical(path, ec);

    if (ec)
        return std::unexpected("canonical failed");

    return fullPath;
}

void CManagerObject::getAppBinary() {
    if (m_pid < 0)
        return;

    auto res = binaryNameForPid(m_pid);

    if (res)
        m_appBinary = *res;
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

    m_tavern.busObject = makeShared<CCHpHyprtavernBusObjectV1Object>(m_tavern.manager->sendGetBusObject("hyprtavern-kv"));

    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_kv_store_v1", KV_PROTOCOL_VERSION, {}, 1);
    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_barmaid_v1", MAID_PROTOCOL_VERSION, {}, 1);

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

    kvImpl = makeShared<CHpHyprtavernKvStoreV1Impl>(1, [this](SP<Hyprwire::IObject> obj) {
        auto x = m_object.managers.emplace_back(makeShared<CManagerObject>(makeShared<CHpHyprtavernKvStoreManagerV1Object>(std::move(obj)))); //
    });

    barmaidImpl = makeShared<CHpHyprtavernBarmaidV1Impl>(1, [this](SP<Hyprwire::IObject> obj) {
        auto x = m_object.barmaidManagers.emplace_back(makeShared<CHpHyprtavernBarmaidManagerV1Object>(std::move(obj))); //
        if (m_object.ready)
            x->sendReady();

        x->setOnDestroy([this, w = WP<CHpHyprtavernBarmaidManagerV1Object>{x}] { std::erase(m_object.barmaidManagers, w); });
    });

    m_object.socket->addImplementation(kvImpl);
    m_object.socket->addImplementation(barmaidImpl);

    auto future = m_kv.init();

    while (true) {
        // TODO: some poll()? will be more loc... I should write a hu helper.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        m_object.socket->dispatchEvents();
        m_tavern.socket->dispatchEvents();

        if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            break;
    }

    if (!future.get())
        return false;

    g_logger->log(LOG_DEBUG, "kv: ready!");
    sendReady();

    return true;
}

void CCore::run() {
    pollfd fds[2] = {
        pollfd{
            .fd     = m_tavern.socket->extractLoopFD(),
            .events = POLLIN,
        },
        pollfd{
            .fd     = m_object.socket->extractLoopFD(),
            .events = POLLIN,
        },
    };

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

SPermData* CCore::permDataFor(SP<Hyprwire::IServerClient> c) {
    for (auto& d : m_permDatas) {
        if (d.client != c)
            continue;

        return &d;
    }

    m_permDatas.emplace_back(SPermData{.client = c});

    return &m_permDatas.back();
}

void CCore::sendReady() {
    m_object.ready = true;
    for (const auto& m : m_object.barmaidManagers) {
        m->sendReady();
    }
}
