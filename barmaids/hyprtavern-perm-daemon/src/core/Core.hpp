#pragma once

#include "../helpers/Memory.hpp"

#include <hp_hyprtavern_core_v1-client.hpp>
#include <hp_hyprtavern_permission_authentication_v1-server.hpp>
#include <hp_hyprtavern_barmaid_v1-server.hpp>

#include <unordered_map>

struct SPermData {
    WP<Hyprwire::IServerClient> client;
    std::string                 tokenUsed;
    std::string                 appIdentifier;
    std::vector<uint32_t>       permissions;
    bool                        appIdentifierPersistent = false;
};

class CTransactionObject {
  public:
    CTransactionObject(SP<CHpHyprtavernPermissionAuthenticationTransactionV1Object>&& obj);
    ~CTransactionObject() = default;

  private:
    SP<CHpHyprtavernPermissionAuthenticationTransactionV1Object> m_object;

    std::string                                                  m_appName, m_appID;
    bool                                                         m_inert = false;
};

class CManagerObject {
  public:
    CManagerObject(SP<CHpHyprtavernPermissionAuthenticationManagerV1Object>&& obj);
    ~CManagerObject() = default;

    void sendAvailability(bool x);

  private:
    SP<CHpHyprtavernPermissionAuthenticationManagerV1Object> m_object;

    SP<SPermData>                                            m_perms;
};

class CCore {
  public:
    CCore()  = default;
    ~CCore() = default;

    CCore(const CCore&) = delete;
    CCore(CCore&)       = delete;
    CCore(CCore&&)      = delete;

    bool init(int fd);
    void run();

    void removeObject(CManagerObject*);
    void removeObject(CTransactionObject*);

    void updateAvailability(bool x);

  private:
    struct {
        SP<Hyprwire::IClientSocket>           socket;
        SP<CCHpHyprtavernCoreManagerV1Object> manager;
        SP<CCHpHyprtavernBusObjectV1Object>   busObject;
    } m_tavern;

    struct {
        SP<Hyprwire::IServerSocket>                          socket;
        std::vector<SP<CManagerObject>>                      managers;
        std::vector<SP<CTransactionObject>>                  transactions;
        std::vector<SP<CHpHyprtavernBarmaidManagerV1Object>> barmaids;
    } m_object;

    std::unordered_map<Hyprwire::IServerClient*, SP<SPermData>> m_permDatas;
    SP<SPermData>                                               permDataFor(const SP<Hyprwire::IServerClient>&);
    void                                                        cleanupPermData();

    friend class CManagerObject;
};

inline UP<CCore> g_core;
