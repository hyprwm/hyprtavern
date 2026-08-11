#include "Core.hpp"
#include "Kv.hpp"
#include "../helpers/Logger.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/poll.h>
#include <unistd.h>

constexpr const uint32_t              TAVERN_PROTOCOL_VERSION = 1;
constexpr const uint32_t              KV_PROTOCOL_VERSION     = 1;
constexpr const uint32_t              MAID_PROTOCOL_VERSION   = 1;
constexpr const size_t                MAX_APP_IDENTIFIER_SIZE = 1024;

static SP<CCHpHyprtavernCoreV1Impl>   impl = makeShared<CCHpHyprtavernCoreV1Impl>(TAVERN_PROTOCOL_VERSION);
static SP<CHpHyprtavernBarmaidV1Impl> barmaidImpl;
static SP<CHpHyprtavernKvStoreV1Impl> kvImpl;

static bool                           hasTavernkeep(const std::vector<uint32_t>& perms) {
    return std::ranges::contains(perms, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP);
}

static void registerAppIdentifierCallback(const SP<CCHpHyprtavernSecurityResponseV1Object>& response, const SP<SPermData>& principal, const int fd) {
    response->setIdentity([principal = WP<SPermData>{principal}, fd](uint32_t, const char* identifier, const char*) {
        auto locked = principal.lock();
        if (!locked)
            return;

        if (!identifier || !*identifier || std::string_view{identifier}.size() > MAX_APP_IDENTIFIER_SIZE) {
            locked->appIdentifier.reset();
            locked->appIdentifierPersistent = false;
            g_logger->log(LOG_WARN, "incoming fd {} has no persistent attested application identifier", fd);
            return;
        }

        locked->appIdentifier           = identifier;
        locked->appIdentifierPersistent = true;
        g_logger->log(LOG_DEBUG, "incoming fd {} received an attested application identifier", fd);
    });
}

CManagerObject::CManagerObject(SP<CHpHyprtavernKvStoreManagerV1Object> obj) : m_object(obj) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_core->removeObject(this); });
    m_perms = g_core->permDataFor(m_object->getObject()->client());

    m_object->setSetValue([this](const char* key, const char* val, hpHyprtavernKvStoreV1ValueType type) {
        if (!g_core->m_kv.isOpen()) {
            m_object->error(HP_HYPRTAVERN_KV_STORE_V1_PROTOCOL_ERRORS_STORE_UNAVAILABLE, "Kv store is locked");
            return;
        }

        const std::string_view keyView = key ? key : "";
        const std::string_view valView = val ? val : "";
        if (auto valid = CKvStore::validateKey(keyView); !valid) {
            m_object->error(-1, valid.error().c_str());
            return;
        }

        std::expected<void, std::string> result;
        switch (type) {
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE: {
                if (!m_perms || !m_perms->appIdentifier || !m_perms->appIdentifierPersistent) {
                    m_object->error(-1, "APP_VALUE requires a persistent attested application identifier");
                    return;
                }

                result = g_core->m_kv.setApp(*m_perms->appIdentifier, keyView, valView);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_UNBOUNDED_VALUE: {
                result = g_core->m_kv.setGlobal(keyView, valView);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE: {
                if (!m_perms || !hasTavernkeep(m_perms->permissions)) {
                    m_object->error(-1, "Insufficient permissions to call set_value with tavern");
                    return;
                }

                result = g_core->m_kv.setTavern(keyView, valView);
                break;
            }
            default: {
                m_object->error(-1, "set_value received an invalid value type");
                return;
            }
        }

        if (!result)
            m_object->error(-1, result.error().c_str());
    });

    m_object->setGetValue([this](const char* key, hpHyprtavernKvStoreV1ValueType type) {
        if (!g_core->m_kv.isOpen()) {
            m_object->error(HP_HYPRTAVERN_KV_STORE_V1_PROTOCOL_ERRORS_STORE_UNAVAILABLE, "Kv store is locked");
            return;
        }

        const std::string_view keyView = key ? key : "";
        if (auto valid = CKvStore::validateKey(keyView); !valid) {
            m_object->error(-1, valid.error().c_str());
            return;
        }

        std::optional<std::string> result;
        switch (type) {
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE: {
                if (!m_perms || !m_perms->appIdentifier || !m_perms->appIdentifierPersistent) {
                    m_object->error(-1, "APP_VALUE requires a persistent attested application identifier");
                    return;
                }

                result = g_core->m_kv.getApp(*m_perms->appIdentifier, keyView);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_UNBOUNDED_VALUE: {
                result = g_core->m_kv.getGlobal(keyView);
                break;
            }
            case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE: {
                if (!m_perms || !hasTavernkeep(m_perms->permissions)) {
                    m_object->error(-1, "Insufficient permissions to call get_value with tavern");
                    return;
                }

                result = g_core->m_kv.getTavern(keyView);
                break;
            }
            default: {
                m_object->error(-1, "get_value received an invalid value type");
                return;
            }
        }

        if (!result)
            m_object->sendValueFailed(keyView.data(), type, HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_VALUE_MISSING);
        else
            m_object->sendValueObtained(keyView.data(), result->c_str(), type);
    });

    if (g_core->m_kv.isOpen())
        sendOpen();
}

CManagerObject::~CManagerObject() = default;

void CManagerObject::sendOpen() {
    m_object->sendStoreAvailable();
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

    m_tavern.manager   = makeShared<CCHpHyprtavernCoreManagerV1Object>(m_tavern.socket->bindProtocol(impl->protocol(), TAVERN_PROTOCOL_VERSION));
    m_tavern.busObject = makeShared<CCHpHyprtavernBusObjectV1Object>(m_tavern.manager->sendGetBusObject("hyprtavern-kv"));

    // The routed new_fd event can arrive as soon as exposure is dispatched, so
    // build the complete internal server before sending any exposure requests.
    m_object.socket = Hyprwire::IServerSocket::open();
    if (!m_object.socket) {
        g_logger->log(LOG_ERR, "failed to create kv server socket");
        return false;
    }

    kvImpl = makeShared<CHpHyprtavernKvStoreV1Impl>(KV_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> obj) {
        m_object.managers.emplace_back(makeShared<CManagerObject>(makeShared<CHpHyprtavernKvStoreManagerV1Object>(std::move(obj))));
    });

    barmaidImpl = makeShared<CHpHyprtavernBarmaidV1Impl>(MAID_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> obj) {
        auto manager = makeShared<CHpHyprtavernBarmaidManagerV1Object>(std::move(obj));
        auto perms   = permDataFor(manager->getObject()->client());

        if (!perms || !hasTavernkeep(perms->permissions)) {
            manager->error(-1, "barmaid protocol requires tavernkeep");
            return;
        }

        auto x = m_object.barmaidManagers.emplace_back(manager);
        if (m_object.ready)
            x->sendReady();

        x->setOnDestroy([this, w = WP<CHpHyprtavernBarmaidManagerV1Object>{x}] { std::erase(m_object.barmaidManagers, w); });
        x->setUpdateTavernEnvironment([this, w = WP<CHpHyprtavernBarmaidManagerV1Object>{x}](const std::vector<const char*>& names, const std::vector<const char*>& values) {
            if (names.size() != values.size()) {
                w->error(-1, "update_tavern_environment with mismatched arrays");
                return;
            }

            g_logger->log(LOG_DEBUG, "kv: updating environment with {} new values", names.size());
            for (size_t i = 0; i < names.size(); ++i) {
                if (!names[i] || !*names[i]) {
                    w->error(-1, "update_tavern_environment received an invalid name");
                    return;
                }

                if (!values[i] || std::string_view{values[i]}.empty())
                    unsetenv(names[i]);
                else
                    setenv(names[i], values[i], true);
            }

            m_kv.onEnvUpdate();
        });
    });

    m_object.socket->addImplementation(kvImpl);
    m_object.socket->addImplementation(barmaidImpl);

    int fds[2] = {-1, -1};
    if (pipe(fds) < 0) {
        g_logger->log(LOG_ERR, "failed to create kv event pipe: {}", std::strerror(errno));
        return false;
    }

    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) {
        g_logger->log(LOG_ERR, "failed to secure kv event pipe: {}", std::strerror(errno));
        close(fds[0]);
        close(fds[1]);
        return false;
    }

    m_kvEventWrite = Hyprutils::OS::CFileDescriptor{fds[1]};
    m_kvEvent      = Hyprutils::OS::CFileDescriptor{fds[0]};

    static bool failedToExpose = false;
    failedToExpose             = false;

    m_tavern.busObject->setExposeProtocolError([](uint32_t err) { failedToExpose = true; });
    m_tavern.busObject->setNewFd([this](int fd, const char* token, const auto&... protocolScopes) {
        auto client = m_object.socket->addClient(fd);
        if (!client) {
            g_logger->log(LOG_ERR, "failed to connect client new fd {}", fd);
            return;
        }

        std::vector<std::string> scope;
        if constexpr (sizeof...(protocolScopes) > 0) {
            auto appendScope = [&scope](const auto& protocolScope) {
                scope.reserve(scope.size() + protocolScope.size());
                for (const auto& protocol : protocolScope) {
                    if (protocol)
                        scope.emplace_back(protocol);
                }
            };
            (appendScope(protocolScopes), ...);

            if (!scope.empty())
                client->setProtocolFilter([scope = std::move(scope)](std::string_view protocol) {
                    return std::ranges::any_of(scope, [protocol](const auto& permitted) { return permitted == protocol; });
                });
        }

        auto principal       = permDataFor(client);
        principal->tokenUsed = token ? token : "";
        if (principal->tokenUsed.empty()) {
            g_logger->log(LOG_ERR, "incoming fd {} has no security token; APP_VALUE will be unavailable", fd);
            return;
        }

        principal->securityResponse = makeShared<CCHpHyprtavernSecurityResponseV1Object>(m_tavern.manager->sendGetSecurityResponse(principal->tokenUsed.c_str()));
        principal->securityResponse->setPermissions([principal = WP<SPermData>{principal}, fd](const std::vector<uint32_t>& perms) {
            auto locked = principal.lock();
            if (!locked)
                return;
            g_logger->log(LOG_DEBUG, "incoming fd {} has {} perms", fd, perms.size());
            locked->permissions = perms;
        });
        registerAppIdentifierCallback(principal->securityResponse, principal, fd);

        m_tavern.socket->roundtrip();
        principal->securityResponse->sendDestroy();
        m_tavern.socket->roundtrip();
        principal->securityResponse.reset();
    });

    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_kv_store_v1", KV_PROTOCOL_VERSION, {}, 1);
    m_tavern.busObject->sendExposeProtocol("hp_hyprtavern_barmaid_v1", MAID_PROTOCOL_VERSION, {HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP}, 0);
    m_tavern.socket->roundtrip();

    if (failedToExpose) {
        g_logger->log(LOG_ERR, "failed to expose kv protocol (is a kv manager running?)");
        return false;
    }

    g_logger->log(LOG_DEBUG, "kv: ready!");
    sendReady();
    m_kv.init();
    return true;
}

void CCore::drainFd(Hyprutils::OS::CFileDescriptor& fd) {
    char   buf[128];
    pollfd pfd = {
        .fd     = fd.get(),
        .events = POLLIN,
    };

    while (fd.isValid()) {
        if (poll(&pfd, 1, 0) < 0 && errno != EINTR)
            return;

        if (pfd.revents & POLLIN) {
            ssize_t bytes = 0;
            do {
                bytes = read(fd.get(), buf, sizeof(buf));
            } while (bytes < 0 && errno == EINTR);
            if (bytes > 0)
                continue;
        }

        break;
    }
}

void CCore::run() {
    pollfd fds[3] = {
        pollfd{
            .fd     = m_tavern.socket->extractLoopFD(),
            .events = POLLIN,
        },
        pollfd{
            .fd     = m_object.socket->extractLoopFD(),
            .events = POLLIN,
        },
        pollfd{
            .fd     = m_kvEvent.get(),
            .events = POLLIN,
        },
    };

    while (true) {
        if (poll(fds, 3, -1) < 0) {
            if (errno == EINTR)
                continue;
            g_logger->log(LOG_ERR, "poll() failed: {}", std::strerror(errno));
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
                g_logger->log(LOG_ERR, "server socket fd dispatch failed");
                return;
            }
        }

        if (fds[2].revents & POLLIN) {
            drainFd(m_kvEvent);
            m_kv.onEvent();
        }

        if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "client socket fd died");
            return;
        }

        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "server socket fd died");
            return;
        }

        if (fds[2].revents & (POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "kv event fd died");
            return;
        }
    }
}

void CCore::sendKvOpen() {
    for (const auto& manager : m_object.managers) {
        manager->sendOpen();
    }
}

void CCore::removeObject(CManagerObject* removed) {
    std::erase_if(m_object.managers, [removed](const auto& manager) { return manager.get() == removed; });
}

SP<SPermData> CCore::permDataFor(SP<Hyprwire::IServerClient> client) {
    std::erase_if(m_permDatas, [](const auto& data) { return !data || !data->client; });

    for (const auto& data : m_permDatas) {
        if (data->client == client)
            return data;
    }

    auto data    = makeShared<SPermData>();
    data->client = client;
    m_permDatas.emplace_back(data);
    return data;
}

void CCore::sendReady() {
    m_object.ready = true;
    for (const auto& manager : m_object.barmaidManagers) {
        manager->sendReady();
    }
}
