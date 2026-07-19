#pragma once

#include <hp_hyprtavern_core_v1-server.hpp>
#include <hp_hyprtavern_core_v1-client.hpp>
#include <hp_hyprtavern_kv_store_v1-client.hpp>
#include <hp_hyprtavern_barmaid_v1-client.hpp>
#include <hp_hyprtavern_permission_authentication_v1-client.hpp>

#include "../helpers/Memory.hpp"
#include "../security/identity/SecurityIdentity.hpp"
#include "../security/Permissions.hpp"

#include <string_view>
#include <unordered_map>

struct SQueryData {
    std::vector<std::string>             protocolNames;
    hpHyprtavernCoreV1BusQueryFilterMode protoFilter = HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL;
    std::vector<std::string>             props;
    hpHyprtavernCoreV1BusQueryFilterMode propFilter = HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL;
};

struct SPersistenceTokenKvData {
    uint32_t              schemaVersion = 1;
    std::string           identity;
    std::vector<uint32_t> persistentPerms;
};

class CSecurityObject;
class CCoreManagerObject;

class CBusQuery {
  public:
    CBusQuery(SP<CHpHyprtavernBusQueryV1Object>&& obj, SQueryData&& data, SP<CCoreManagerObject> manager);
    ~CBusQuery() = default;

    SQueryData m_data;

  private:
    SP<CHpHyprtavernBusQueryV1Object> m_object;
    WP<CCoreManagerObject>            m_manager;
};

class CBusObject {
  public:
    CBusObject(SP<CHpHyprtavernBusObjectV1Object>&& obj, const char* name, SP<CCoreManagerObject> manager);
    ~CBusObject() = default;

    void sendNewConnection(int fd, const std::string& token, const std::vector<std::string>& protocolScope);

    struct SProtocolExposeData {
        std::string           name;
        uint32_t              rev = 0;
        std::vector<uint32_t> perms;
    };

    std::vector<SProtocolExposeData>                 m_protocols;
    std::vector<std::pair<std::string, std::string>> m_props;
    std::vector<uint32_t>                            m_requiredPerms;

    std::string                                      m_name;

    size_t                                           m_internalID = 0;

  private:
    SP<CHpHyprtavernBusObjectV1Object> m_object;
    WP<CCoreManagerObject>             m_owner;
};

class CCoreManagerObject {
  public:
    CCoreManagerObject(SP<CHpHyprtavernCoreManagerV1Object>&& obj);
    ~CCoreManagerObject() = default;

    std::string                             m_associatedSecurityToken;
    UP<Security::ISecurityIdentityProvider> m_securityProvider;
    bool                                    m_isInternal = false;

    WP<CCoreManagerObject>                  m_self;
    WP<CSecurityObject>                     m_security;

    bool                                    hasPerm(hpHyprtavernCoreV1SecurityPermissionType type);
    bool                                    hasPerm(uint32_t type);
    bool                                    hasAllPerms(const std::vector<uint32_t>& perms);
    void                                    sendReady();

    bool                                    m_readySent = false;

  private:
    SP<CHpHyprtavernCoreManagerV1Object> m_object;
};

class CSecurityObject {
  public:
    CSecurityObject(SP<CHpHyprtavernSecurityObjectV1Object>&& obj, SP<CCoreManagerObject> manager, const std::string& token);
    ~CSecurityObject() = default;

    std::string             m_token, m_name, m_description;
    WP<CCoreManagerObject>  m_manager;
    int                     m_pid = -1;
    std::vector<uint32_t>   m_sessionPerms;
    SPersistenceTokenKvData m_kvData;

    WP<CSecurityObject>     m_self;

  private:
    SP<CHpHyprtavernSecurityObjectV1Object> m_object;
};

class CSecurityResponse {
  public:
    CSecurityResponse(SP<CHpHyprtavernSecurityResponseV1Object>&& obj, const std::string& oneTimeToken);
    ~CSecurityResponse() = default;

    WP<CSecurityObject> m_security;

  private:
    SP<CHpHyprtavernSecurityResponseV1Object> m_object;
};

class CBusObjectHandle {
  public:
    CBusObjectHandle(SP<CHpHyprtavernBusObjectHandleV1Object>&& obj, SP<CBusObject> busObject, SP<CCoreManagerObject> manager);
    ~CBusObjectHandle() = default;

    WP<CBusObject>         m_busObject;
    WP<CCoreManagerObject> m_manager;

  private:
    SP<CHpHyprtavernBusObjectHandleV1Object> m_object;
};

class CCoreProtocolHandler {
  public:
    CCoreProtocolHandler()  = default;
    ~CCoreProtocolHandler() = default;

    bool init(SP<Hyprwire::IServerSocket> sock);
    bool initBarmaids();
    bool initKv();
    bool initPd();
    bool initInternalCoreClient();
    void sendReady();

    void registerInternalClient(SP<Hyprwire::IServerClient> client);
    bool isInternalClient(SP<Hyprwire::IServerClient> client);
    bool isReservedProtocol(std::string_view protocol);

    //
    void removeObject(CCoreManagerObject* obj);
    void removeObject(CBusObject* obj);
    void removeObject(CBusObjectHandle* obj);
    void removeObject(CBusQuery* obj);
    void removeObject(CSecurityObject* obj);
    void removeObject(CSecurityResponse* obj);

    //
    std::vector<SP<CCoreManagerObject>>                                        m_managers;
    std::vector<SP<CBusObject>>                                                m_objects;
    std::vector<SP<CBusObjectHandle>>                                          m_handles;
    std::vector<SP<CBusQuery>>                                                 m_queries;
    std::vector<SP<CSecurityObject>>                                           m_securityObjects;
    std::vector<SP<CSecurityResponse>>                                         m_securityResponses;
    std::vector<SP<CCHpHyprtavernPermissionAuthenticationTransactionV1Object>> m_transactions;
    std::vector<WP<Hyprwire::IServerClient>>                                   m_internalClients;

    SP<CBusObject>                                                             fromID(uint32_t id);

    WP<Hyprwire::IServerSocket>                                                m_sock;

    struct {
        SP<Hyprwire::IClientSocket>                               sock;
        SP<Hyprwire::IClientSocket>                               kvSock;
        SP<Hyprwire::IClientSocket>                               pdSock;

        SP<CCHpHyprtavernCoreManagerV1Object>                     coreManager;
        SP<CCHpHyprtavernKvStoreManagerV1Object>                  kvManager;
        SP<CCHpHyprtavernBarmaidManagerV1Object>                  kvBarmaidManager;
        SP<CCHpHyprtavernPermissionAuthenticationManagerV1Object> pdManager;
        SP<CCHpHyprtavernBarmaidManagerV1Object>                  pdBarmaidManager;
        bool                                                      kvOpen = false;
        WP<Hyprwire::IServerClient>                               wireClient;
    } m_client;

    std::string                                  m_tavernkeepToken = "__tavernkeep__";

    std::unordered_map<std::string, std::string> m_oneTimeTokenMap;

    bool                                         m_barmaidsReady = false;

    std::string                                  generateToken();
};

inline UP<CCoreProtocolHandler> g_coreProto;
