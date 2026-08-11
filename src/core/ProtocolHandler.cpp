#include "ProtocolHandler.hpp"
#include "BarmaidConnector.hpp"
#include "../helpers/Logger.hpp"
#include "../security/Permissions.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <chrono>
#include <string_view>

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
constexpr const std::array<const char*, 3> RESERVED_PROTOCOLS  = {"hp_hyprtavern_kv_store_v1", "hp_hyprtavern_permission_authentication_v1", "hp_hyprtavern_barmaid_v1"};
constexpr const size_t                     MAX_ONE_TIME_TOKENS = 4096;
constexpr const auto                       ONE_TIME_TOKEN_TTL  = std::chrono::seconds{30};

namespace {
    bool validQueryMode(hpHyprtavernCoreV1BusQueryFilterMode mode) {
        return mode == HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL || mode == HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ANY;
    }

    bool knownPermissionDaemonResult(uint32_t result) {
        return result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_ONCE ||
            result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_PERSISTENT || result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED ||
            result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_UNAVAILABLE;
    }

    void appendUnique(std::vector<uint32_t>& destination, const std::vector<uint32_t>& source) {
        for (const auto permission : source) {
            if (!std::ranges::contains(destination, permission))
                destination.emplace_back(permission);
        }
    }
}

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

    if (!validQueryMode(m_data.protoFilter) || !validQueryMode(m_data.propFilter)) {
        m_object->error(-1, "Invalid query filter mode");
        return;
    }

    if (std::ranges::any_of(m_data.props, [](const auto& property) { return !property.contains('='); })) {
        m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_MANAGER_ERRORS_INVALID_PROPERTY, "Invalid property in query");
        return;
    }

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
        const std::string_view protocolName = name ? std::string_view{name} : std::string_view{};
        if (protocolName.empty() || std::ranges::any_of(requiredPerms, [](const auto permission) { return !Security::isKnownPermission(permission); })) {
            m_object->error(-1, "invalid protocol exposure data");
            return;
        }

        if (g_coreProto->isReservedProtocol(protocolName)) {
            auto owner = m_owner.lock();
            if (!owner || !owner->hasPerm(HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP)) {
                m_object->error(-1, "reserved protocols can only be exposed by tavernkeep clients");
                return;
            }
        }

        const bool requestedExclusive = exclusiveMode != 0;
        for (const auto& object : g_coreProto->m_objects) {
            const bool conflicts = std::ranges::any_of(object->m_protocols, [&protocolName, requestedExclusive](const auto& exposed) {
                return exposed.name == protocolName && (requestedExclusive || exposed.exclusive);
            });
            if (!conflicts)
                continue;

            m_object->sendExposeProtocolError(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_EXPOSE_ERRORS_ALREADY_EXPOSED);
            return;
        }

        m_protocols.emplace_back(SProtocolExposeData{.name = std::string{protocolName}, .rev = rev, .perms = requiredPerms, .exclusive = requestedExclusive});
    });

    m_object->setRequirePermissions([this](const std::vector<uint32_t>& perms) {
        if (std::ranges::any_of(perms, [](const auto permission) { return !Security::isKnownPermission(permission); })) {
            m_object->error(-1, "invalid required permission");
            return;
        }
        m_requiredPerms = perms;
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

        for (auto& [n, v] : m_props) {
            if (n != name)
                continue;

            v = value;
            return;
        }

        m_props.emplace_back(std::make_pair<>(name, value));
    });
}

SP<CCoreManagerObject> CBusObject::owner() const {
    return m_owner.lock();
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

        auto recipient = m_busObject->owner();
        if (!recipient) {
            m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR);
            return;
        }

        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            g_logger->log(LOG_ERR, "failed to create a socketpair");
            m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR);
            return;
        }

        const auto oneTimeToken = g_coreProto->issueConnectionToken(manager, recipient);
        if (oneTimeToken.empty()) {
            close(fds[0]);
            close(fds[1]);
            m_object->sendSocketFailed(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR);
            return;
        }

        m_object->sendSocket(fds[0]);
        m_busObject->sendNewConnection(fds[1], oneTimeToken, protocolScope);

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
    m_authority  = m_isInternal ? Security::ePrincipalAuthority::INTERNAL : Security::ePrincipalAuthority::EXTERNAL;

    m_securityProvider = Security::identify(client, m_authority);

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
    });

    m_object->setGetSecurityResponse([this](uint32_t seq, const char* token) {
        g_coreProto->m_securityResponses.emplace_back( //
            makeShared<CSecurityResponse>(             //
                makeShared<CHpHyprtavernSecurityResponseV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_security_response_v1", seq)), //
                token,                                                                                                                                     //
                m_self.lock()                                                                                                                              //
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

bool CCoreManagerObject::hasExplicitPerm(uint32_t type) const {
    if (!m_security)
        return false;

    return Security::permissionListHas(m_security->m_sessionPerms, type) || Security::permissionListHas(m_security->m_kvData.persistentPerms, type);
}

bool CCoreManagerObject::permissionGrantedByPolicy(uint32_t type) const {
    if (!Security::isExternallyRequestablePermission(type) || !g_coreProto->m_securityPolicy.nonSandboxedAppsBypassPermissions || !m_securityProvider)
        return false;

    return m_securityProvider->classification() == Security::eIdentityClass::HOST_SAME_UID && m_securityProvider->trustworthy();
}

bool CCoreManagerObject::hasPersistentIdentity() const {
    return m_securityProvider && m_securityProvider->trustworthy() && m_securityProvider->appIDPersistent();
}

bool CCoreManagerObject::hasPerm(uint32_t type) {
    if (!Security::isKnownPermission(type))
        return false;

    if (m_authority == Security::ePrincipalAuthority::INTERNAL)
        return true;

    if (!Security::isExternallyRequestablePermission(type))
        return false;

    return hasExplicitPerm(type) || permissionGrantedByPolicy(type);
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

    if (!manager || !g_coreProto->m_client.kvOpen || !g_coreProto->m_client.kvManager || !g_coreProto->m_client.kvSock ||
        (manager->m_authority == Security::ePrincipalAuthority::EXTERNAL && !g_coreProto->m_barmaidsReady)) {
        m_object->sendUnavailable();
        return;
    }

    if (manager)
        m_pid = manager->m_securityProvider->pid();

    m_object->setSetIdentity([this](const char* name, const char* desc) {
        const std::string_view appName        = name ? name : "";
        const std::string_view appDescription = desc ? desc : "";
        if (appName.size() > 256 || appDescription.size() > 2048) {
            m_object->error(-1, "security identity is too large");
            return;
        }

        m_name                  = appName;
        m_description           = appDescription;
        m_kvData.schemaVersion  = 2;
        m_kvData.appName        = m_name;
        m_kvData.appDescription = m_description;

        auto manager = m_manager.lock();
        if (m_token.empty() || !manager || !manager->hasPersistentIdentity() || !g_coreProto->m_client.kvManager)
            return;

        const auto fullTokenKey = std::format("token:{}", m_token);
        if (auto serialized = glz::write_json(m_kvData); serialized)
            g_coreProto->m_client.kvManager->sendSetValue(fullTokenKey.c_str(), serialized->c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
        else
            g_logger->log(LOG_ERR, "failed to persist security identity for token");
    });

    m_object->setObtainPermission([this](hpHyprtavernCoreV1SecurityPermissionType type, hpHyprtavernCoreV1SecurityPermissionMode mode) {
        const auto requestedPermission = static_cast<uint32_t>(type);
        const auto requestedMode       = static_cast<uint32_t>(mode);
        auto       manager             = m_manager.lock();

        if (!manager || !Security::isExternallyRequestablePermission(requestedPermission) || !Security::isKnownPermissionMode(requestedMode)) {
            m_object->sendPermissionResult(type, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
            return;
        }

        if (manager->hasExplicitPerm(requestedPermission)) {
            m_object->sendPermissionResult(type, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_ALREADY_GRANTED);
            return;
        }

        if (manager->permissionGrantedByPolicy(requestedPermission)) {
            m_object->sendPermissionResult(type, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED_BY_POLICY);
            return;
        }

        if (!g_coreProto->m_barmaidsReady || !g_coreProto->m_client.pdManager || !g_coreProto->m_client.pdOpen) {
            m_object->sendPermissionResult(type, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
            return;
        }

        auto object = g_coreProto->m_transactions.emplace_back(
            makeShared<CCHpHyprtavernPermissionAuthenticationTransactionV1Object>(g_coreProto->m_client.pdManager->sendInitPermissionTransaction()));

        const bool        persistenceAllowed = requestedMode == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT && manager->hasPersistentIdentity();
        const auto        effectiveMode      = persistenceAllowed ? requestedMode : static_cast<uint32_t>(HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_SESSION);

        const std::string appName = m_name.empty() ? "Unknown application" : m_name;
        std::string       appID   = "unknown";
        if (manager->m_securityProvider->appID())
            appID = *manager->m_securityProvider->appID();
        else if (manager->m_securityProvider->path())
            appID = *manager->m_securityProvider->path();

        object->setPermissionResult([w = WP<CSecurityObject>{m_self}, object, requestedPermission, effectiveMode](uint32_t permission, uint32_t result) {
            CScopeGuard cleanup([&object] {
                object->sendDestroy();
                std::erase(g_coreProto->m_transactions, object);
            });

            auto        self = w.lock();
            if (!self)
                return;

            if (permission != requestedPermission || !knownPermissionDaemonResult(result)) {
                self->m_object->sendPermissionResult(static_cast<hpHyprtavernCoreV1SecurityPermissionType>(requestedPermission),
                                                     HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
                return;
            }

            if (result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED) {
                self->m_object->sendPermissionResult(static_cast<hpHyprtavernCoreV1SecurityPermissionType>(requestedPermission),
                                                     HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_DENIED);
                return;
            }

            if (result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_UNAVAILABLE) {
                self->m_object->sendPermissionResult(static_cast<hpHyprtavernCoreV1SecurityPermissionType>(requestedPermission),
                                                     HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
                return;
            }

            if (effectiveMode == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT &&
                result == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_PERSISTENT) {
                if (!Security::permissionListHas(self->m_kvData.persistentPerms, requestedPermission))
                    self->m_kvData.persistentPerms.emplace_back(requestedPermission);

                if (auto sourceManager = self->m_manager.lock(); sourceManager)
                    self->m_kvData.identity = sourceManager->m_securityProvider->identity();

                self->m_kvData.schemaVersion  = 2;
                self->m_kvData.appName        = self->m_name;
                self->m_kvData.appDescription = self->m_description;

                const auto fullTokenKey = std::format("token:{}", self->m_token);
                auto       serialized   = glz::write_json(self->m_kvData);
                if (serialized && g_coreProto->m_client.kvManager)
                    g_coreProto->m_client.kvManager->sendSetValue(fullTokenKey.c_str(), serialized->c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
                else if (!serialized)
                    g_logger->log(LOG_ERR, "failed to serialize persistent permissions for token");
            } else if (!Security::permissionListHas(self->m_sessionPerms, requestedPermission))
                self->m_sessionPerms.emplace_back(requestedPermission);

            self->m_object->sendPermissionResult(static_cast<hpHyprtavernCoreV1SecurityPermissionType>(requestedPermission),
                                                 HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED);
        });

        object->sendSetAppName(appName.c_str());
        object->sendSetAppIdentifier(appID.c_str());
        object->sendAskPermission(type, static_cast<hpHyprtavernPermissionAuthenticationV1AskType>(effectiveMode));
    });

    if (!token.empty() && manager->hasPersistentIdentity()) {
        const auto                 fullTokenKey = std::format("token:{}", token);
        std::optional<std::string> data;
        bool                       matchingResponseReceived = false;

        g_coreProto->m_client.kvManager->setValueObtained([&data, &matchingResponseReceived, fullTokenKey](const char* key, const char* value, uint32_t valueType) {
            if (!key || std::string_view{key} != fullTokenKey || valueType != HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE)
                return;
            matchingResponseReceived = true;
            data                     = value ? std::string{value} : std::string{};
        });
        CScopeGuard clearKvCallback([] {
            if (g_coreProto && g_coreProto->m_client.kvManager)
                g_coreProto->m_client.kvManager->setValueObtained([](const char*, const char*, uint32_t) {});
        });

        g_coreProto->m_client.kvManager->sendGetValue(fullTokenKey.c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
        g_coreProto->m_client.kvSock->roundtrip();

        if (!matchingResponseReceived || !data || data->empty())
            g_logger->log(LOG_DEBUG, "received no matching kv value for security token");
        else {
            auto parsed = glz::read_json<SPersistenceTokenKvData>(*data);
            if (!parsed || (parsed->schemaVersion != 1 && parsed->schemaVersion != 2))
                g_logger->log(LOG_DEBUG, "kv returned an invalid or unsupported security-token record, minting a fresh token");
            else {
                auto       sourceManager         = m_manager.lock();
                const auto currentIdentity       = sourceManager ? sourceManager->m_securityProvider->identity() : std::string{};
                bool       legacyIdentityMatches = false;
                if (sourceManager && parsed->schemaVersion == 1 && sourceManager->m_securityProvider->classification() == Security::eIdentityClass::HOST_SAME_UID &&
                    sourceManager->m_securityProvider->path()) {
                    const auto legacyIdentity = std::format("naive:path={}:uid={}:gid={}:chrooted=false", *sourceManager->m_securityProvider->path(), getuid(), getgid());
                    legacyIdentityMatches     = parsed->identity == legacyIdentity;
                }

                if (!sourceManager || (parsed->identity != currentIdentity && !legacyIdentityMatches))
                    g_logger->log(LOG_DEBUG, "security token identity mismatch, minting a fresh token");
                else {
                    parsed->schemaVersion   = 2;
                    parsed->identity        = currentIdentity;
                    parsed->persistentPerms = Security::sanitizeExternalPermissions(parsed->persistentPerms);
                    m_token                 = token;
                    m_kvData                = *parsed;
                    m_name                  = parsed->appName;
                    m_description           = parsed->appDescription;

                    if (legacyIdentityMatches) {
                        if (auto serialized = glz::write_json(m_kvData); serialized)
                            g_coreProto->m_client.kvManager->sendSetValue(fullTokenKey.c_str(), serialized->c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
                    }
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

CSecurityResponse::CSecurityResponse(SP<CHpHyprtavernSecurityResponseV1Object>&& obj, const std::string& oneTimeToken, SP<CCoreManagerObject> recipient) :
    m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_connection = g_coreProto->consumeConnectionToken(oneTimeToken, recipient);
    if (!m_connection) {
        m_object->sendFailed();
        return;
    }

    m_security = m_connection->principal.security;
    m_object->setRequery([this] { sendData(); });
    sendData();
}

void CSecurityResponse::sendData() {
    if (!m_connection) {
        m_object->sendFailed();
        return;
    }

    if (auto source = m_connection->source.lock(); source)
        m_connection->principal = g_coreProto->principalFor(source);

    const auto& principal = m_connection->principal;
    m_security            = principal.security;

    const auto& appIdentifier = principal.appIdentifierPersistent ? principal.appIdentifier : std::string{};
    m_object->sendIdentity(principal.pid < 0 ? 0U : static_cast<uint32_t>(principal.pid), appIdentifier.c_str(), principal.description.c_str());
    m_object->sendPermissions(principal.permissions);
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

SConnectionPrincipal CCoreProtocolHandler::principalFor(SP<CCoreManagerObject> manager) const {
    SConnectionPrincipal principal;
    if (!manager)
        return principal;

    principal.authority = manager->m_authority;
    if (manager->m_securityProvider) {
        principal.classification          = manager->m_securityProvider->classification();
        principal.pid                     = manager->m_securityProvider->pid();
        principal.identity                = manager->m_securityProvider->identity();
        principal.appIdentifierPersistent = manager->hasPersistentIdentity();
        if (manager->m_securityProvider->appID())
            principal.appIdentifier = *manager->m_securityProvider->appID();
        else if (manager->m_securityProvider->path())
            principal.appIdentifier = *manager->m_securityProvider->path();
        if (manager->m_securityProvider->displayName())
            principal.name = *manager->m_securityProvider->displayName();
    }

    if (principal.appIdentifier.empty())
        principal.appIdentifier = "unknown";
    if (principal.name.empty())
        principal.name = "Unknown application";

    if (manager->m_authority == Security::ePrincipalAuthority::INTERNAL) {
        principal.name                    = "hyprtavern";
        principal.description             = "Hyprtavern internal authority";
        principal.appIdentifier           = "hyprtavern";
        principal.appIdentifierPersistent = true;
        principal.permissions             = {HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP};
        return principal;
    }

    if (auto security = manager->m_security.lock(); security) {
        principal.security    = security;
        principal.name        = security->m_name.empty() ? principal.name : security->m_name;
        principal.description = security->m_description;
        principal.permissions = Security::sanitizeExternalPermissions(security->m_sessionPerms);
        appendUnique(principal.permissions, Security::sanitizeExternalPermissions(security->m_kvData.persistentPerms));
    }

    if (manager->permissionGrantedByPolicy(HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS))
        appendUnique(principal.permissions, Security::externallyRequestablePermissionGroups());

    return principal;
}

void CCoreProtocolHandler::pruneConnectionTokens() {
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(m_oneTimeTokenMap, [&now](const auto& entry) { return entry.second.expiresAt <= now || !entry.second.expectedRecipient; });
}

std::string CCoreProtocolHandler::issueConnectionToken(SP<CCoreManagerObject> source, SP<CCoreManagerObject> expectedRecipient) {
    if (!source || !expectedRecipient)
        return {};

    pruneConnectionTokens();
    while (m_oneTimeTokenMap.size() >= MAX_ONE_TIME_TOKENS) {
        const auto oldest = std::ranges::min_element(m_oneTimeTokenMap, {}, [](const auto& entry) { return entry.second.expiresAt; });
        if (oldest == m_oneTimeTokenMap.end())
            return {};
        m_oneTimeTokenMap.erase(oldest);
    }

    const auto token = generateToken();
    m_oneTimeTokenMap.emplace(token,
                              SOneTimeConnectionToken{
                                  .principal         = principalFor(source),
                                  .source            = source,
                                  .expectedRecipient = expectedRecipient,
                                  .expiresAt         = std::chrono::steady_clock::now() + ONE_TIME_TOKEN_TTL,
                              });
    return token;
}

std::optional<SOneTimeConnectionToken> CCoreProtocolHandler::consumeConnectionToken(const std::string& token, SP<CCoreManagerObject> recipient) {
    pruneConnectionTokens();
    const auto entry = m_oneTimeTokenMap.find(token);
    if (entry == m_oneTimeTokenMap.end() || !recipient)
        return std::nullopt;

    auto expectedRecipient = entry->second.expectedRecipient.lock();
    if (!expectedRecipient) {
        m_oneTimeTokenMap.erase(entry);
        return std::nullopt;
    }

    if (expectedRecipient != recipient)
        return std::nullopt;

    auto connection = std::move(entry->second);
    m_oneTimeTokenMap.erase(entry);
    return connection;
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
            g_logger->log(LOG_DEBUG, "CCoreProtocolHandler::initKv: kv barmaid ready (store {})", m_client.kvOpen ? "available" : "unavailable");
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
    m_client.pdManager->setAvailability([this](uint32_t available) {
        m_client.pdOpen              = available != 0;
        m_client.pdAvailabilityKnown = true;
    });

    while (true) {
        if (!m_client.pdSock->dispatchEvents(true)) {
            g_logger->log(LOG_ERR, "CCoreProtocolHandler::initPd: failed, barmaid died");
            return false;
        }

        if (maidReady && m_client.pdAvailabilityKnown) {
            g_logger->log(LOG_DEBUG, "CCoreProtocolHandler::initPd: pd barmaid ready (permission prompts {})", m_client.pdOpen ? "available" : "unavailable");
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
