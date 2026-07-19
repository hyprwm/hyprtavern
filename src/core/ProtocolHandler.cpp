#include "ProtocolHandler.hpp"
#include "BarmaidConnector.hpp"
#include "../helpers/Logger.hpp"
#include "../security/Permissions.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <random>
#include <limits>

#include <hyprutils/utils/ScopeGuard.hpp>
using namespace Hyprutils::Utils;

#include <sys/socket.h>
#include <sys/poll.h>
#include <uuid.h>
#include <unistd.h>
#include <glaze/glaze.hpp>

constexpr const uint32_t                                TAVERN_PROTOCOL_VERSION = 1;
constexpr const uint32_t                                KV_PROTOCOL_VERSION     = 1;
constexpr const uint32_t                                PD_PROTOCOL_VERSION     = 1;
constexpr const uint32_t                                MAID_PROTOCOL_VERSION   = 1;

static SP<CCHpHyprtavernCoreV1Impl>                     clientCoreImpl    = makeShared<CCHpHyprtavernCoreV1Impl>(TAVERN_PROTOCOL_VERSION);
static SP<CCHpHyprtavernKvStoreV1Impl>                  clientKvImpl      = makeShared<CCHpHyprtavernKvStoreV1Impl>(KV_PROTOCOL_VERSION);
static SP<CCHpHyprtavernPermissionAuthenticationV1Impl> clientPdImpl      = makeShared<CCHpHyprtavernPermissionAuthenticationV1Impl>(PD_PROTOCOL_VERSION);
static SP<CCHpHyprtavernBarmaidV1Impl>                  clientBarmaidImpl = makeShared<CCHpHyprtavernBarmaidV1Impl>(MAID_PROTOCOL_VERSION);
static SP<CHpHyprtavernCoreV1Impl>                      coreImpl;
static uint32_t                                         maxId = 1;

constexpr const std::array<const char*, 2>              ENV_FREE_TO_UPDATE = {"WAYLAND_DISPLAY", "DISPLAY"};
constexpr const std::array<const char*, 3> RESERVED_PROTOCOLS = {"hp_hyprtavern_kv_store_v1", "hp_hyprtavern_permission_authentication_v1", "hp_hyprtavern_barmaid_v1"};

//

CBusQuery::CBusQuery(SP<CHpHyprtavernBusQueryV1Object>&& obj, SQueryData&& data, SP<CCoreManagerObject> manager) :
    m_data(std::move(data)), m_object(std::move(obj)), m_manager(manager) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    g_logger->log(LOG_DEBUG, "new query with {} protocols and {} props", m_data.protocolNames.size(), m_data.props.size());

    // run the query
    std::vector<uint32_t> matches;
    auto                  managerSP = m_manager.lock();

    for (const auto& obj : g_coreProto->m_objects) {
        if (!managerSP || !managerSP->hasAllPerms(obj->m_requiredPerms))
            continue;

        auto protocolVisible = [&managerSP](const CBusObject::SProtocolExposeData& p) { return managerSP->hasAllPerms(p.perms); };

        // protocols
        if (!m_data.protocolNames.empty()) {
            if (m_data.protoFilter == HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL) {
                bool matched = true;
                for (const auto& p : m_data.protocolNames) {
                    if (std::ranges::find_if(obj->m_protocols, [&p, &protocolVisible](const auto& e) { return e.name == p && protocolVisible(e); }) != obj->m_protocols.end())
                        continue;

                    matched = false;
                    break;
                }

                if (!matched)
                    continue;
            } else {
                bool matched = false;
                for (const auto& p : m_data.protocolNames) {
                    if (std::ranges::find_if(obj->m_protocols, [&p, &protocolVisible](const auto& e) { return e.name == p && protocolVisible(e); }) == obj->m_protocols.end())
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

CBusObject::CBusObject(SP<CHpHyprtavernBusObjectV1Object>&& obj, const char* name, SP<CCoreManagerObject> manager) : m_name(name), m_object(std::move(obj)), m_owner(manager) {
    if (!m_object->getObject())
        return;

    m_internalID = maxId++;

    g_logger->log(LOG_DEBUG, "new bus object gets id {}", m_internalID);

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setExposeProtocol([this](const char* name, uint32_t rev, const std::vector<uint32_t>& requiredPerms, uint32_t exclusiveMode) {
        if (g_coreProto->isReservedProtocol(name)) {
            auto owner = m_owner.lock();
            if (!owner || !owner->hasPerm(HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP)) {
                m_object->error(-1, "reserved protocols can only be exposed by tavernkeep clients");
                return;
            }
        }

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

    m_object->setRequirePermissions([this](const std::vector<uint32_t>& perms) { m_requiredPerms = perms; });

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

        for (auto& [n, v] : m_props) {
            if (n != name)
                continue;

            v = value;
            return;
        }

        m_props.emplace_back(std::make_pair<>(name, value));
    });
}

void CBusObject::sendNewConnection(int fd, const std::string& token, const std::vector<std::string>& protocolScope) {
    std::vector<const char*> scope;
    scope.reserve(protocolScope.size());

    for (const auto& p : protocolScope) {
        scope.emplace_back(p.c_str());
    }

    m_object->sendNewFd(fd, token.c_str(), scope);
}

CBusObjectHandle::CBusObjectHandle(SP<CHpHyprtavernBusObjectHandleV1Object>&& obj, SP<CBusObject> busObject, SP<CCoreManagerObject> manager) :
    m_busObject(busObject), m_manager(manager), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setConnect([this](const std::vector<const char*>& requestedProtocols) {
        if (!m_busObject) {
            m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR);
            return;
        }

        auto manager = m_manager.lock();
        if (!manager || !manager->hasAllPerms(m_busObject->m_requiredPerms)) {
            m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_INSUFFICIENT_PERMISSIONS);
            return;
        }

        if (requestedProtocols.empty()) {
            m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_BAD_PROTOCOL);
            return;
        }

        std::vector<std::string> protocolScope;
        protocolScope.reserve(requestedProtocols.size());

        for (const auto& requested : requestedProtocols) {
            if (std::ranges::contains(protocolScope, requested))
                continue;

            auto protocol = std::ranges::find_if(m_busObject->m_protocols, [requested](const auto& p) { return p.name == requested; });

            if (protocol == m_busObject->m_protocols.end()) {
                m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_BAD_PROTOCOL);
                return;
            }

            if (!manager->hasAllPerms(protocol->perms)) {
                m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_INSUFFICIENT_PERMISSIONS);
                return;
            }

            protocolScope.emplace_back(requested);
        }

        int fds[2];

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            g_logger->log(LOG_ERR, "failed to create a socketpair");
            m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR);
            return;
        }

        m_object->sendSocket(fds[0]);

        if (m_manager->m_associatedSecurityToken.empty())
            m_busObject->sendNewConnection(fds[1], "", protocolScope);
        else {
            // FIXME: small leak. Clean up uuids after object is gone?
            auto uuid                            = g_coreProto->generateToken();
            g_coreProto->m_oneTimeTokenMap[uuid] = m_manager->m_associatedSecurityToken;
            m_busObject->sendNewConnection(fds[1], uuid, protocolScope);
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

    auto owner = m_manager.lock();
    if (!owner || !owner->hasAllPerms(m_busObject->m_requiredPerms)) {
        g_logger->log(LOG_DEBUG, "new object handle rejected for object id {} due to missing permissions", m_busObject->m_internalID);
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
            if (!owner->hasAllPerms(p.perms))
                continue;

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

    auto client  = m_object->getObject()->client();
    m_isInternal = g_coreProto->isInternalClient(client) || client == g_coreProto->m_client.wireClient;

    m_securityProvider = Security::identify(client);

    if (m_isInternal)
        m_associatedSecurityToken = g_coreProto->m_tavernkeepToken;

    m_object->setGetBusObject([this](uint32_t seq, const char* objectName) {
        if (!g_coreProto->m_barmaidsReady && !m_isInternal) {
            m_object->error(-1, "tavern is still initializing");
            return;
        }

        g_coreProto->m_objects.emplace_back( //
            makeShared<CBusObject>(          //
                makeShared<CHpHyprtavernBusObjectV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_object_v1", seq)), //
                objectName,                                                                                                                         //
                m_self.lock()                                                                                                                       //
                ));
    });

    m_object->setGetObjectHandle([this](uint32_t seq, uint32_t id) {
        if (!g_coreProto->m_barmaidsReady && !m_isInternal) {
            m_object->error(-1, "tavern is still initializing");
            return;
        }

        g_coreProto->m_handles.emplace_back( //
            makeShared<CBusObjectHandle>(    //
                makeShared<CHpHyprtavernBusObjectHandleV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_object_handle_v1", seq)), //
                g_coreProto->fromID(id),                                                                                                                   //
                m_self.lock()                                                                                                                              //
                ));
    });

    m_object->setGetQueryObject([this](uint32_t seq, std::vector<const char*> protos, hpHyprtavernCoreV1BusQueryFilterMode protoMode, std::vector<const char*> props,
                                       hpHyprtavernCoreV1BusQueryFilterMode propMode) {
        if (!g_coreProto->m_barmaidsReady && !m_isInternal) {
            m_object->error(-1, "tavern is still initializing");
            return;
        }

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
                std::move(data),                                                                                                                   //
                m_self.lock()                                                                                                                      //
                ));
    });

    m_object->setGetSecurityObject([this](uint32_t seq, const char* token) {
        if (m_security) {
            m_object->error(-1, "manager already has a security object");
            return;
        }

        auto x = g_coreProto->m_securityObjects.emplace_back( //
            makeShared<CSecurityObject>(                      //
                makeShared<CHpHyprtavernSecurityObjectV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_security_object_v1", seq)), //
                m_self.lock(),                                                                                                                           //
                token                                                                                                                                    //
                ));

        m_security = x;
        x->m_self  = x;
        if (!x->m_token.empty())
            m_associatedSecurityToken = x->m_token;
    });

    m_object->setGetSecurityResponse([this](uint32_t seq, const char* token) {
        g_coreProto->m_securityResponses.emplace_back( //
            makeShared<CSecurityResponse>(             //
                makeShared<CHpHyprtavernSecurityResponseV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_security_response_v1", seq)), //
                token                                                                                                                                      //
                ));
    });

    m_object->setUpdateTavernEnvironment([this](const std::vector<const char*>& names, const std::vector<const char*>& values) {
        const bool ANY_ENV_PROTECTED =
            !std::ranges::all_of(names, [](const auto& name) { return std::ranges::any_of(ENV_FREE_TO_UPDATE, [&name](const auto& e) { return std::string_view{e} == name; }); });

        if (ANY_ENV_PROTECTED && !hasPerm(HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MANAGEMENT_ENVIRONMENT)) {
            m_object->error(-1, "update_tavern_environment requires a management_environment permission");
            return;
        }

        if (names.size() != values.size()) {
            m_object->error(-1, "update_tavern_environment with mismatched arrays");
            return;
        }

        g_logger->log(LOG_DEBUG, "updating environment: {} new values", names.size());

        // update barmaids
        if (g_coreProto->m_client.kvBarmaidManager)
            g_coreProto->m_client.kvBarmaidManager->sendUpdateTavernEnvironment(names, values);
        if (g_coreProto->m_client.pdBarmaidManager)
            g_coreProto->m_client.pdBarmaidManager->sendUpdateTavernEnvironment(names, values);

        // update ourselves
        for (size_t i = 0; i < names.size(); ++i) {
            if (std::string_view{values[i]}.empty())
                unsetenv(names[i]);
            else
                setenv(names[i], values[i], true);
        }
    });
}

bool CCoreManagerObject::hasPerm(hpHyprtavernCoreV1SecurityPermissionType type) {
    return hasPerm(sc<uint32_t>(type));
}

bool CCoreManagerObject::hasPerm(uint32_t type) {
    if (m_isInternal || m_associatedSecurityToken == g_coreProto->m_tavernkeepToken)
        return true;

    if (m_security) {
        if (Security::permissionListHas(m_security->m_sessionPerms, type))
            return true;

        if (Security::permissionListHas(m_security->m_kvData.persistentPerms, type))
            return true;
    }

// FIXME: configurabel!
#define NON_SANDBOXED_BYPASS true

    if (type != HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP && NON_SANDBOXED_BYPASS && m_securityProvider->type() == Security::PROVIDER_NAIVE)
        return true;

    return false;
}

bool CCoreManagerObject::hasAllPerms(const std::vector<uint32_t>& perms) {
    for (const auto& p : perms) {
        if (!hasPerm(p))
            return false;
    }

    return true;
}

void CCoreManagerObject::sendReady() {
    if (m_object && !m_readySent) {
        m_object->sendReady();
        m_readySent = true;
    }
}

CSecurityObject::CSecurityObject(SP<CHpHyprtavernSecurityObjectV1Object>&& obj, SP<CCoreManagerObject> manager, const std::string& token) :
    m_manager(manager), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    if (!g_coreProto->m_client.kvOpen) {
        m_object->sendUnavailable();
        return;
    }

    if (manager)
        m_pid = manager->m_securityProvider->pid();

    m_object->setSetIdentity([this](const char* name, const char* desc) {
        m_name        = name;
        m_description = desc;
    });

    m_object->setObtainPermission([this](hpHyprtavernCoreV1SecurityPermissionType type, hpHyprtavernCoreV1SecurityPermissionMode mode) {
        auto manager = m_manager.lock();
        if (manager && manager->hasPerm(type)) {
            m_object->sendPermissionResult(type, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_ALREADY_GRANTED);
            return;
        }

        auto object = g_coreProto->m_transactions.emplace_back(
            makeShared<CCHpHyprtavernPermissionAuthenticationTransactionV1Object>(g_coreProto->m_client.pdManager->sendInitPermissionTransaction()));

        std::string appName = m_name.empty() ? "Unknown application" : m_name;
        std::string appID   = "unknown";
        if (manager) {
            if (manager->m_securityProvider->appID())
                appID = *manager->m_securityProvider->appID();
            else if (manager->m_securityProvider->path())
                appID = *manager->m_securityProvider->path();
        }

        object->sendSetAppName(appName.c_str());
        object->sendSetAppIdentifier(appID.c_str());
        object->sendAskPermission(type, sc<hpHyprtavernPermissionAuthenticationV1AskType>(mode));

        object->setPermissionResult([this, w = WP<CSecurityObject>{m_self}, object, mode](uint32_t perm, uint32_t result) {
            CScopeGuard x([&object] {
                object->sendDestroy();
                std::erase(g_coreProto->m_transactions, object);
            });

            if (!w)
                return;

            if (result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED) {
                m_object->sendPermissionResult(perm, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_DENIED);
                return;
            }

            if (result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_UNAVAILABLE) {
                m_object->sendPermissionResult(perm, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
                return;
            }

            if (mode == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT && result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_PERSISTENT) {
                if (!Security::permissionListHas(m_kvData.persistentPerms, perm))
                    m_kvData.persistentPerms.emplace_back(perm);

                if (auto manager = m_manager.lock(); manager)
                    m_kvData.identity = manager->m_securityProvider->identity();

                const auto FULL_TOKEN_K = std::format("token:{}", m_token);
                auto       serialized   = glz::write_json(m_kvData);
                if (serialized)
                    g_coreProto->m_client.kvManager->sendSetValue(FULL_TOKEN_K.c_str(), serialized->c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
                else
                    g_logger->log(LOG_ERR, "failed to serialize persistent permissions for token");
            } else if (!Security::permissionListHas(m_sessionPerms, perm))
                m_sessionPerms.emplace_back(perm);

            m_object->sendPermissionResult(perm, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED);

            return;
        });
    });

    if (!token.empty()) {
        // try to find the token in the kv
        const auto  FULL_TOKEN_K = std::format("token:{}", token);

        std::string data;

        g_coreProto->m_client.kvManager->sendGetValue(FULL_TOKEN_K.c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
        g_coreProto->m_client.kvManager->setValueObtained([&data](const char* k, const char* v, uint32_t type) { data = v; });

        g_coreProto->m_client.kvSock->roundtrip();

        if (data.empty())
            g_logger->log(LOG_DEBUG, "received a token that is not in our kv, probably empty");
        else {
            auto parsed = glz::read_json<SPersistenceTokenKvData>(data);
            if (!parsed) {
                g_logger->log(LOG_DEBUG, "kv returned a broken response for token, resetting");
                auto serialized = glz::write_json(SPersistenceTokenKvData{});
                if (serialized)
                    g_coreProto->m_client.kvManager->sendSetValue(FULL_TOKEN_K.c_str(), serialized->c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
            } else {
                // parsed successfully
                auto manager = m_manager.lock();
                if (!manager || parsed->identity != manager->m_securityProvider->identity())
                    g_logger->log(LOG_DEBUG, "security token identity mismatch, minting a fresh token");
                else {
                    m_token  = token;
                    m_kvData = *parsed;
                }
            }
        }
    }

    if (m_token.empty()) {
        m_token = g_coreProto->generateToken();
        if (manager)
            m_kvData.identity = manager->m_securityProvider->identity();
    }

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
    registerInternalClient(m_client.wireClient.lock());

    return true;
}

void CCoreProtocolHandler::registerInternalClient(SP<Hyprwire::IServerClient> client) {
    if (!client)
        return;

    if (isInternalClient(client))
        return;

    m_internalClients.emplace_back(client);
}

bool CCoreProtocolHandler::isInternalClient(SP<Hyprwire::IServerClient> client) {
    if (!client)
        return false;

    std::erase_if(m_internalClients, [](const auto& c) { return !c; });

    for (const auto& c : m_internalClients) {
        if (c == client)
            return true;
    }

    return false;
}

bool CCoreProtocolHandler::isReservedProtocol(std::string_view protocol) {
    return std::ranges::any_of(RESERVED_PROTOCOLS, [&protocol](const auto& p) { return protocol == p; });
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

bool CCoreProtocolHandler::initInternalCoreClient() {
    if (m_client.coreManager)
        return true;

    if (!m_client.sock->waitForHandshake()) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initInternalCoreClient: tavern handshake failed");
        return false;
    }

    m_client.sock->addImplementation(clientCoreImpl);

    const auto SPEC = m_client.sock->getSpec(clientCoreImpl->protocol()->specName());

    if (!SPEC) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initInternalCoreClient: failed because tavern doesn't support tavern proto??");
        return false;
    }

    m_client.coreManager = makeShared<CCHpHyprtavernCoreManagerV1Object>(m_client.sock->bindProtocol(clientCoreImpl->protocol(), TAVERN_PROTOCOL_VERSION));

    return true;
}

bool CCoreProtocolHandler::initKv() {
    if (!initInternalCoreClient())
        return false;

    const auto FD = CBarmaidConnector::connectToProtocol(m_client.sock, m_client.coreManager, "hp_hyprtavern_kv_store_v1", {"hp_hyprtavern_barmaid_v1"});
    if (!FD) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initKv: {}", FD.error());
        return false;
    }

    m_client.kvSock = Hyprwire::IClientSocket::open(*FD);

    if (!m_client.kvSock->waitForHandshake()) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initKv: handshake failed");
        return false;
    }

    m_client.kvSock->addImplementation(clientKvImpl);
    m_client.kvSock->addImplementation(clientBarmaidImpl);

    // handshake is estabilished

    m_client.kvManager        = makeShared<CCHpHyprtavernKvStoreManagerV1Object>(m_client.kvSock->bindProtocol(clientKvImpl->protocol(), KV_PROTOCOL_VERSION));
    m_client.kvBarmaidManager = makeShared<CCHpHyprtavernBarmaidManagerV1Object>(m_client.kvSock->bindProtocol(clientBarmaidImpl->protocol(), MAID_PROTOCOL_VERSION));

    bool maidReady = false;

    m_client.kvBarmaidManager->setReady([&maidReady] { maidReady = true; });
    m_client.kvManager->setStoreAvailable([this] { m_client.kvOpen = true; });

    while (true) {
        if (!m_client.kvSock->dispatchEvents(true)) {
            g_logger->log(LOG_ERR, "CCoreProtocolHandler::initKv: failed, barmaid died");
            return false;
        }

        if (maidReady) {
            g_logger->log(LOG_DEBUG, "CCoreProtocolHandler::initKv: kv barmaid ready");
            break;
        }
    }

    return true;
}

bool CCoreProtocolHandler::initPd() {
    if (!initInternalCoreClient())
        return false;

    const auto FD = CBarmaidConnector::connectToProtocol(m_client.sock, m_client.coreManager, "hp_hyprtavern_permission_authentication_v1", {"hp_hyprtavern_barmaid_v1"});
    if (!FD) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initPd: {}", FD.error());
        return false;
    }

    m_client.pdSock = Hyprwire::IClientSocket::open(*FD);

    if (!m_client.pdSock->waitForHandshake()) {
        g_logger->log(LOG_ERR, "CCoreProtocolHandler::initPd: handshake failed");
        return false;
    }

    m_client.pdSock->addImplementation(clientPdImpl);
    m_client.pdSock->addImplementation(clientBarmaidImpl);

    // handshake is estabilished

    m_client.pdManager        = makeShared<CCHpHyprtavernPermissionAuthenticationManagerV1Object>(m_client.pdSock->bindProtocol(clientPdImpl->protocol(), PD_PROTOCOL_VERSION));
    m_client.pdBarmaidManager = makeShared<CCHpHyprtavernBarmaidManagerV1Object>(m_client.pdSock->bindProtocol(clientBarmaidImpl->protocol(), MAID_PROTOCOL_VERSION));

    bool maidReady = false;

    m_client.pdBarmaidManager->setReady([&maidReady] { maidReady = true; });

    while (true) {
        if (!m_client.pdSock->dispatchEvents(true)) {
            g_logger->log(LOG_ERR, "CCoreProtocolHandler::initPd: failed, barmaid died");
            return false;
        }

        if (maidReady) {
            g_logger->log(LOG_DEBUG, "CCoreProtocolHandler::initPd: pd barmaid ready");
            break;
        }
    }

    return true;
}

bool CCoreProtocolHandler::initBarmaids() {
    m_barmaidsReady = initKv() && initPd();
    return m_barmaidsReady;
}

void CCoreProtocolHandler::sendReady() {
    for (const auto& m : m_managers) {
        if (m)
            m->sendReady();
    }
}
