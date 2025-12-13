#include "ProtocolHandler.hpp"
#include "../helpers/Logger.hpp"

#include <algorithm>
#include <format>
#include <random>
#include <limits>

#include <sys/socket.h>
#include <sys/poll.h>
#include <uuid.h>
#include <glaze/glaze.hpp>

constexpr const uint32_t               TAVERN_PROTOCOL_VERSION = 1;
constexpr const uint32_t               KV_PROTOCOL_VERSION     = 1;
constexpr const uint32_t               MAID_PROTOCOL_VERSION   = 1;

static SP<CCHpHyprtavernCoreV1Impl>    clientCoreImpl    = makeShared<CCHpHyprtavernCoreV1Impl>(TAVERN_PROTOCOL_VERSION);
static SP<CCHpHyprtavernKvStoreV1Impl> clientKvImpl      = makeShared<CCHpHyprtavernKvStoreV1Impl>(KV_PROTOCOL_VERSION);
static SP<CCHpHyprtavernBarmaidV1Impl> clientBarmaidImpl = makeShared<CCHpHyprtavernBarmaidV1Impl>(MAID_PROTOCOL_VERSION);
static SP<CHpHyprtavernCoreV1Impl>     coreImpl;
static uint32_t                        maxId = 1;

//

CBusQuery::CBusQuery(SP<CHpHyprtavernBusQueryV1Object>&& obj, SQueryData&& data) : m_data(std::move(data)), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    g_logger->log(LOG_DEBUG, "new query with {} protocols and {} props", m_data.protocolNames.size(), m_data.props.size());

    // run the query
    std::vector<uint32_t> matches;
    for (const auto& obj : g_coreProto->m_objects) {

        // protocols
        if (!m_data.protocolNames.empty()) {
            if (m_data.protoFilter == HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL) {
                bool matched = true;
                for (const auto& p : m_data.protocolNames) {
                    if (std::ranges::find_if(obj->m_protocols, [&p](const auto& e) { return e.name == p; }) != obj->m_protocols.end())
                        continue;

                    matched = false;
                    break;
                }

                if (!matched)
                    continue;
            } else {
                bool matched = false;
                for (const auto& p : m_data.protocolNames) {
                    if (std::ranges::find_if(obj->m_protocols, [&p](const auto& e) { return e.name == p; }) == obj->m_protocols.end())
                        continue;

                    matched = true;
                    break;
                }

                if (!matched)
                    continue;
            }
        }

        // properties
        if (!m_data.props.empty()) {
            if (m_data.propFilter == HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL) {
                bool matched = true;
                for (const auto& p : m_data.props) {
                    if (!p.contains('=')) {
                        m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property in query");
                        return;
                    }
                    size_t           eqPos    = p.find('=');
                    std::string_view propName = std::string_view(p).substr(0, eqPos);
                    std::string_view propVal  = std::string_view(p).substr(eqPos + 1);

                    if (std::ranges::find_if(obj->m_props, [&propName, &propVal](const auto& e) { return e.first == propName && e.second == propVal; }) != obj->m_props.end())
                        continue;

                    matched = false;
                    break;
                }

                if (!matched)
                    continue;
            } else {
                bool matched = false;
                for (const auto& p : m_data.props) {
                    if (!p.contains('=')) {
                        m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property in query");
                        return;
                    }
                    size_t           eqPos    = p.find('=');
                    std::string_view propName = std::string_view(p).substr(0, eqPos);
                    std::string_view propVal  = std::string_view(p).substr(eqPos + 1);

                    if (std::ranges::find_if(obj->m_props, [&propName, &propVal](const auto& e) { return e.first == propName && e.second == propVal; }) == obj->m_props.end())
                        continue;

                    matched = true;
                    break;
                }

                if (!matched)
                    continue;
            }
        }

        // matched
        matches.emplace_back(obj->m_internalID);
    }

    g_logger->log(LOG_DEBUG, "query got {} matches", matches.size());

    // send the matches
    m_object->sendResults(matches);
}

CBusObject::CBusObject(SP<CHpHyprtavernBusObjectV1Object>&& obj, const char* name) : m_name(name), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_internalID = maxId++;

    g_logger->log(LOG_DEBUG, "new bus object gets id {}", m_internalID);

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setExposeProtocol([this](const char* name, uint32_t rev, const std::vector<uint32_t>& requiredPerms, uint32_t exclusiveMode) {
        if (!exclusiveMode) {
            m_protocols.emplace_back(SProtocolExposeData{.name = name, .rev = rev, .perms = requiredPerms});
            return;
        }

        // exclusive mode: check if this protocol is not already on the bus.
        for (const auto& o : g_coreProto->m_objects) {
            const bool HAS = std::ranges::any_of(o->m_protocols, [&name](const auto& e) { return e.name == name; });
            if (HAS) {
                // send an error, already taken, ignore this request
                m_object->sendExposeProtocolError(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_EXPOSE_ERRORS_ALREADY_EXPOSED);
                return;
            }
        }

        // pass: register
        m_protocols.emplace_back(SProtocolExposeData{.name = name, .rev = rev, .perms = requiredPerms});
    });

    m_object->setExposeProperty([this](const char* n, const char* v) {
        std::string_view name  = n;
        std::string_view value = v;

        if (name.empty()) {
            m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property name (empty)");
            return;
        }

        if (!std::ranges::all_of(name,
                                 [](const char& c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '+' || c == ':' || (c >= '0' && c <= '9'); })) {
            m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property name (invalid chars)");
            return;
        }

        if (std::ranges::count(name, ':') != 1 || name.front() == ':' || name.back() == ':') {
            m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property name (invalid colons)");
            return;
        }

        if (value.empty()) {
            std::erase_if(m_props, [&name](const auto& e) { return e.first == name; });
            return;
        }

        m_props.emplace_back(std::make_pair<>(name, value));
    });
}

void CBusObject::sendNewConnection(int fd, const std::string& token) {
    m_object->sendNewFd(fd, token.c_str());
}

CBusObjectHandle::CBusObjectHandle(SP<CHpHyprtavernBusObjectHandleV1Object>&& obj, SP<CBusObject> busObject) : m_busObject(busObject), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    // FIXME: perms!!!!

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setConnect([this]() {
        if (!m_busObject) {
            m_object->sendSocketFailed();
            return;
        }

        int fds[2];

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            g_logger->log(LOG_ERR, "failed to create a socketpair");
            m_object->sendSocketFailed();
            return;
        }

        m_object->sendSocket(fds[0]);

        if (m_manager->m_associatedSecurityToken.empty())
            m_busObject->sendNewConnection(fds[1], "");
        else {
            // FIXME: small leak. Clean up uuids after object is gone?
            auto uuid                            = g_coreProto->generateToken();
            g_coreProto->m_oneTimeTokenMap[uuid] = m_manager->m_associatedSecurityToken;
            m_busObject->sendNewConnection(fds[1], uuid);
        }

        close(fds[0]);
        close(fds[1]);
    });

    // send the data about the object

    if (!m_busObject) {
        g_logger->log(LOG_DEBUG, "new object handle for invalid object");
        m_object->sendFailed();
        return;
    }

    g_logger->log(LOG_DEBUG, "new object handle for object id {}", m_busObject->m_internalID);

    m_object->sendName(m_busObject->m_name.c_str());

    {
        std::vector<const char*> names;
        std::vector<uint32_t>    revs;

        names.reserve(m_busObject->m_protocols.size());
        revs.reserve(m_busObject->m_protocols.size());

        for (const auto& p : m_busObject->m_protocols) {
            // FIXME: perms!!!
            names.emplace_back(p.name.c_str());
            revs.emplace_back(p.rev);
        }

        m_object->sendProtocols(names, revs);
    }

    {
        std::vector<std::string> container;
        std::vector<const char*> strs;

        container.reserve(m_busObject->m_props.size());
        strs.reserve(m_busObject->m_props.size());

        for (const auto& [n, v] : m_busObject->m_props) {
            container.emplace_back(std::format("{}={}", n, v));
            strs.emplace_back(container.back().c_str());
        }

        m_object->sendProperties(strs);
    }

    m_object->sendDone();
}

CCoreManagerObject::CCoreManagerObject(SP<CHpHyprtavernCoreManagerV1Object>&& obj) : m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    if (m_object->getObject()->client() == g_coreProto->m_client.wireClient)
        m_associatedSecurityToken = g_coreProto->m_tavernkeepToken;

    m_object->setGetBusObject([this](uint32_t seq, const char* objectName) {
        g_coreProto->m_objects.emplace_back( //
            makeShared<CBusObject>(          //
                makeShared<CHpHyprtavernBusObjectV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_object_v1", seq)), //
                objectName                                                                                                                          //
                ));
    });

    m_object->setGetObjectHandle([this](uint32_t seq, uint32_t id) {
        auto x       = g_coreProto->m_handles.emplace_back( //
            makeShared<CBusObjectHandle>(             //
                makeShared<CHpHyprtavernBusObjectHandleV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_object_handle_v1", seq)), //
                g_coreProto->fromID(id)                                                                                                                    //
                ));
              x->m_manager = m_self;
    });

    m_object->setGetQueryObject([this](uint32_t seq, std::vector<const char*> protos, hpHyprtavernCoreV1BusQueryFilterMode protoMode, std::vector<const char*> props,
                                       hpHyprtavernCoreV1BusQueryFilterMode propMode) {
        SQueryData data;
        data.propFilter  = propMode;
        data.protoFilter = protoMode;

        data.props.reserve(props.size());
        data.protocolNames.reserve(protos.size());

        for (const auto& pn : protos) {
            data.protocolNames.emplace_back(pn);
        }

        for (const auto& pn : props) {
            data.props.emplace_back(pn);
        }

        g_coreProto->m_queries.emplace_back( //
            makeShared<CBusQuery>(           //
                makeShared<CHpHyprtavernBusQueryV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_query_v1", seq)), //
                std::move(data)                                                                                                                    //
                ));
    });

    m_object->setGetSecurityObject([this](uint32_t seq, const char* token) {
        auto x = g_coreProto->m_securityObjects.emplace_back( //
            makeShared<CSecurityObject>(                      //
                makeShared<CHpHyprtavernSecurityObjectV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_security_object_v1", seq)), //
                m_self.lock(),                                                                                                                           //
                token                                                                                                                                    //
                ));
    });

    m_object->setGetSecurityResponse([this](uint32_t seq, const char* token) {
        auto x = g_coreProto->m_securityResponses.emplace_back( //
            makeShared<CSecurityResponse>(                      //
                makeShared<CHpHyprtavernSecurityResponseV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_security_response_v1", seq)), //
                token                                                                                                                                      //
                ));
    });
}

CSecurityObject::CSecurityObject(SP<CHpHyprtavernSecurityObjectV1Object>&& obj, SP<CCoreManagerObject> manager, const std::string& token) :
    m_manager(manager), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setSetIdentity([this](const char* name, const char* desc) {
        m_name        = name;
        m_description = desc;
    });

    m_object->setObtainPermission([this](hpHyprtavernCoreV1SecurityPermissionType type, hpHyprtavernCoreV1SecurityPermissionMode mode) {
        // FIXME: impl thsi!!!!!! aBwgcvfdtyfwefctywevtyfwecvtyfgecvtyfgwetyfgvh FYUCKCKCKCKCK FUCKER

        g_logger->log(LOG_WARN, "FIXME: obtain_permission is not impl'd I am a lazy fuck!");

        m_sessionPerms.emplace_back(type);

        m_object->sendPermissionResult(type, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED_BY_POLICY);

        // FIXME: send to kv persistent perms
    });

    if (!token.empty()) {
        // try to find the token in the kv
        const auto  FULL_TOKEN_K = std::format("token:{}", token);

        std::string data;

        g_coreProto->m_client.kvManager->sendGetValue(FULL_TOKEN_K.c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
        g_coreProto->m_client.kvManager->setValueObtained([&data](const char* k, const char* v, uint32_t type) { data = v; });

        g_coreProto->m_sock->dispatchEvents();

        if (data.empty())
            g_logger->log(LOG_DEBUG, "received a token that is not in our kv, probably empty");
        else {
            // found the token
            m_token = token;

            auto parsed = glz::read_json<SPersistenceTokenKvData>(data);
            if (!parsed) {
                g_logger->log(LOG_DEBUG, "kv returned a broken response for token, resetting");
                g_coreProto->m_client.kvManager->sendSetValue(FULL_TOKEN_K.c_str(), glz::write_json(SPersistenceTokenKvData{})->c_str(),
                                                              HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
            } else {
                // parsed successfully
                m_kvData = *parsed;
            }
        }
    }

    if (m_token.empty())
        m_token = g_coreProto->generateToken();

    m_object->sendToken(m_token.c_str());
}

CSecurityResponse::CSecurityResponse(SP<CHpHyprtavernSecurityResponseV1Object>&& obj, const std::string& oneTimeToken) : m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    if (!g_coreProto->m_oneTimeTokenMap.contains(oneTimeToken)) {
        m_object->sendFailed();
        return;
    }

    auto token = g_coreProto->m_oneTimeTokenMap[oneTimeToken];
    g_coreProto->m_oneTimeTokenMap.erase(oneTimeToken);

    if (token == g_coreProto->m_tavernkeepToken) {
        m_object->setRequery([this] {
            m_object->sendIdentity(getpid(), "hyprtavern", "Hyprtavern's tavernkeep");
            m_object->sendPermissions({HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP}); // FIXME: should have all perms
            m_object->sendDone();
        });

        m_object->sendIdentity(getpid(), "hyprtavern", "Hyprtavern's tavernkeep");
        m_object->sendPermissions({HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP}); // FIXME: should have all perms
        m_object->sendDone();
        return;
    }

    // find the object we are interested in
    for (const auto& s : g_coreProto->m_securityObjects) {
        if (s->m_token != token)
            continue;

        m_security = s;
        break;
    }

    if (!m_security) {
        m_object->sendFailed();
        return;
    }

    m_object->setRequery([this] {
        if (!m_security) {
            m_object->sendFailed();
            return;
        }

        std::vector<uint32_t> perms = m_security->m_sessionPerms;
        perms.reserve(m_security->m_sessionPerms.size() + m_security->m_kvData.persistentPerms.size());
        perms.append_range(m_security->m_kvData.persistentPerms);

        m_object->sendIdentity(m_security->m_pid, m_security->m_name.c_str(), m_security->m_description.c_str());
        m_object->sendPermissions(perms);
        m_object->sendDone();
    });

    std::vector<uint32_t> perms = m_security->m_sessionPerms;
    perms.reserve(m_security->m_sessionPerms.size() + m_security->m_kvData.persistentPerms.size());
    perms.append_range(m_security->m_kvData.persistentPerms);

    m_object->sendIdentity(m_security->m_pid, m_security->m_name.c_str(), m_security->m_description.c_str());
    m_object->sendPermissions(perms);
    m_object->sendDone();
}

bool CCoreProtocolHandler::init(SP<Hyprwire::IServerSocket> sock) {
    coreImpl = makeShared<CHpHyprtavernCoreV1Impl>(TAVERN_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> obj) {
        auto x    = m_managers.emplace_back(makeShared<CCoreManagerObject>(makeShared<CHpHyprtavernCoreManagerV1Object>(std::move(obj))));
        x->m_self = x;
    });

    sock->addImplementation(coreImpl);

    m_sock = sock;

    // init object and connect to ourselves

    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::init: failed to create a socketpair");
        return false;
    }

    m_client.sock = Hyprwire::IClientSocket::open(fds[1]);

    {
        std::random_device              dev;
        std::mt19937_64                 engine(dev());
        std::uniform_int_distribution<> distribution(0ULL, std::numeric_limits<int>::max());

        m_tavernkeepToken = std::format("__tavernkeep__{}_{}__", distribution(engine), distribution(engine));
    }

    m_client.wireClient = m_sock->addClient(fds[0]);

    return true;
}

void CCoreProtocolHandler::removeObject(CCoreManagerObject* obj) {
    std::erase_if(m_managers, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CBusQuery* obj) {
    std::erase_if(m_queries, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CBusObject* obj) {
    std::erase_if(m_objects, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CBusObjectHandle* obj) {
    std::erase_if(m_handles, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CSecurityObject* obj) {
    std::erase_if(m_securityObjects, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CSecurityResponse* obj) {
    std::erase_if(m_securityResponses, [obj](const auto& e) { return e.get() == obj; });
}

SP<CBusObject> CCoreProtocolHandler::fromID(uint32_t id) {
    for (const auto& o : m_objects) {
        if (o->m_internalID != id)
            continue;

        return o;
    }

    return nullptr;
}

std::string CCoreProtocolHandler::generateToken() {
    std::string uuid;
    do {
        uuid_t uuid_;
        uuid_generate_random(uuid_);
        uuid = std::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", sc<uint16_t>(uuid_[0]), sc<uint16_t>(uuid_[1]),
                           sc<uint16_t>(uuid_[2]), sc<uint16_t>(uuid_[3]), sc<uint16_t>(uuid_[4]), sc<uint16_t>(uuid_[5]), sc<uint16_t>(uuid_[6]), sc<uint16_t>(uuid_[7]),
                           sc<uint16_t>(uuid_[8]), sc<uint16_t>(uuid_[9]), sc<uint16_t>(uuid_[10]), sc<uint16_t>(uuid_[11]), sc<uint16_t>(uuid_[12]), sc<uint16_t>(uuid_[13]),
                           sc<uint16_t>(uuid_[14]), sc<uint16_t>(uuid_[15]));
    } while (m_oneTimeTokenMap.contains(uuid));

    return uuid;
}

bool CCoreProtocolHandler::initBarmaids() {
    if (!m_client.sock->waitForHandshake()) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initBarmaids: tavern handshake failed");
        return false;
    }

    m_client.sock->addImplementation(clientCoreImpl);

    const auto SPEC = m_client.sock->getSpec(clientCoreImpl->protocol()->specName());

    if (!SPEC) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initBarmaids: failed because tavern doesn't support tavern proto??");
        return false;
    }

    // get the handle
    auto manager = makeShared<CCHpHyprtavernCoreManagerV1Object>(m_client.sock->bindProtocol(clientCoreImpl->protocol(), TAVERN_PROTOCOL_VERSION));

    auto query = makeShared<CCHpHyprtavernBusQueryV1Object>(
        manager->sendGetQueryObject({"hp_hyprtavern_kv_store_v1"}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL, {}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL));

    int fd = -1;

    query->setResults([this, &manager, &fd](const std::vector<uint32_t>& res) {
        if (res.empty())
            return;

        auto handle = makeShared<CCHpHyprtavernBusObjectHandleV1Object>(manager->sendGetObjectHandle(res[0]));
        handle->setSocket([&fd](int connFd) { fd = connFd; });
        handle->sendConnect();

        m_client.sock->roundtrip();
    });

    m_client.sock->roundtrip();

    if (fd < 0) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initBarmaids: failed cuz bus has no kv?");
        return false;
    }

    m_client.kvSock = Hyprwire::IClientSocket::open(fd);

    if (!m_client.kvSock->waitForHandshake()) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initBarmaids: handshake failed");
        return false;
    }

    m_client.kvSock->addImplementation(clientKvImpl);
    m_client.kvSock->addImplementation(clientBarmaidImpl);

    // handshake is estabilished

    m_client.kvManager        = makeShared<CCHpHyprtavernKvStoreManagerV1Object>(m_client.kvSock->bindProtocol(clientKvImpl->protocol(), KV_PROTOCOL_VERSION));
    m_client.kvBarmaidManager = makeShared<CCHpHyprtavernBarmaidManagerV1Object>(m_client.kvSock->bindProtocol(clientBarmaidImpl->protocol(), MAID_PROTOCOL_VERSION));

    bool maidReady = false;

    m_client.kvBarmaidManager->setReady([&maidReady] { maidReady = true; });

    while (true) {
        if (!m_client.kvSock->dispatchEvents(true)) {
            g_logger->log(LOG_ERR, "CCoreProtocolHandler::initBarmaids: failed, barmaid died");
            return false;
        }

        if (maidReady) {
            g_logger->log(LOG_DEBUG, "CCoreProtocolHandler::initBarmaids: kv barmaid ready");
            break;
        }
    }

    return true;
}
