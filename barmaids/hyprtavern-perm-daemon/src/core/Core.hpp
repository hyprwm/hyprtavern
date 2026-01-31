#pragma once

#include "../helpers/Memory.hpp"

#include <hp_hyprtavern_core_v1-client.hpp>
#include <hp_hyprtavern_permission_authentication_v1-server.hpp>
#include <hp_hyprtavern_barmaid_v1-server.hpp>

struct SPermData {
    WP<Hyprwire::IServerClient> client;
    std::string                 tokenUsed;
    std::vector<uint32_t>       permissions;
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
    ~CManagerObject();

    void sendAvailability(bool x);

  private:
    SP<CHpHyprtavernPermissionAuthenticationManagerV1Object> m_object;

    SPermData                                                m_perms;
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

    std::vector<SPermData> m_permDatas;
    SPermData*             permDataFor(SP<Hyprwire::IServerClient>);

    friend class CManagerObject;
};

inline UP<CCore> g_core;
