#pragma once

#include "../helpers/Memory.hpp"

#include "Kv.hpp"

#include <hp_hyprtavern_core_v1-client.hpp>
#include <hp_hyprtavern_kv_store_v1-server.hpp>
#include <hp_hyprtavern_barmaid_v1-server.hpp>

#include <hyprutils/os/FileDescriptor.hpp>

struct SPermData {
    WP<Hyprwire::IServerClient>                client;
    std::string                                tokenUsed;
    std::vector<uint32_t>                      permissions;
    std::optional<std::string>                 appIdentifier;
    bool                                       appIdentifierPersistent = false;
    SP<CCHpHyprtavernSecurityResponseV1Object> securityResponse;
};

class CManagerObject {
  public:
    CManagerObject(SP<CHpHyprtavernKvStoreManagerV1Object> obj);
    ~CManagerObject();

    void sendOpen();

  private:
    SP<CHpHyprtavernKvStoreManagerV1Object> m_object;
    SP<SPermData>                           m_perms;
};

class CCore {
  public:
    CCore()  = default;
    ~CCore() = default;

    CCore(const CCore&) = delete;
    CCore(CCore&)       = delete;
    CCore(CCore&&)      = delete;

    bool                           init(int fd);
    void                           run();

    void                           removeObject(CManagerObject*);
    void                           sendKvOpen();

    Hyprutils::OS::CFileDescriptor m_kvEvent, m_kvEventWrite;

  private:
    void sendReady();
    void drainFd(Hyprutils::OS::CFileDescriptor& fd);

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

    CKvStore                   m_kv;

    std::vector<SP<SPermData>> m_permDatas;
    SP<SPermData>              permDataFor(SP<Hyprwire::IServerClient>);

    friend class CManagerObject;
};

inline UP<CCore> g_core;
