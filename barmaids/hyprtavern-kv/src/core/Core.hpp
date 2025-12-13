#pragma once

#include "../helpers/Memory.hpp"

#include "Kv.hpp"

#include <hp_hyprtavern_core_v1-client.hpp>
#include <hp_hyprtavern_kv_store_v1-server.hpp>
#include <hp_hyprtavern_barmaid_v1-server.hpp>

struct SPermData {
    WP<Hyprwire::IServerClient> client;
    std::string                 tokenUsed;
    std::vector<uint32_t>       permissions;
};

class CManagerObject {
  public:
    CManagerObject(SP<CHpHyprtavernKvStoreManagerV1Object> obj);
    ~CManagerObject();

  private:
    void                                    getAppBinary();

    SP<CHpHyprtavernKvStoreManagerV1Object> m_object;

    SPermData                               m_perms;
    int                                     m_pid = -1;

    std::string                             m_appBinary = "anonymous";
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

  private:
    void sendReady();

    struct {
        SP<Hyprwire::IClientSocket>           socket;
        SP<CCHpHyprtavernCoreManagerV1Object> manager;
        SP<CCHpHyprtavernBusObjectV1Object>   busObject;
    } m_tavern;

    struct {
        SP<Hyprwire::IServerSocket>                          socket;
        std::vector<SP<CManagerObject>>                      managers;
        std::vector<SP<CHpHyprtavernBarmaidManagerV1Object>> barmaidManagers;
        bool                                                 ready = false;
    } m_object;

    CKvStore               m_kv;

    std::vector<SPermData> m_permDatas;
    SPermData*             permDataFor(SP<Hyprwire::IServerClient>);

    friend class CManagerObject;
};

inline UP<CCore> g_core;
