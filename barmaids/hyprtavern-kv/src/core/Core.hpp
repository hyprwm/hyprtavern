#pragma once

#include "../store/Store.hpp"

#include <common/EventLoop.hpp>
#include <common/Memory.hpp>
#include <shared/BarmaidServer.hpp>

#include <hp_hyprtavern_core_v1-client.hpp>

#include <hyprutils/os/FileDescriptor.hpp>
#include <hyprwire/hyprwire.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// what a connection may access, from its attested principal
struct SAccess {
    // the tavern's internal authority, required for tavern-scoped values
    bool tavern = false;

    // scope of the app's own values, empty without a stable attested identity
    std::string appScope;
};

class CCore {
  public:
    CCore(UP<Kv::CStore>&& store);
    ~CCore();

    bool           init(int controlFd, int tavernFd);
    bool           run();

    const SAccess* access(const SP<Hyprwire::IServerClient>& client) const;

    UP<Kv::CStore> m_store;

  private:
    struct SPendingConnection {
        Hyprutils::OS::CFileDescriptor                fd;
        SP<CCHpHyprtavernConnectionPrincipalV1Object> principal;
        SAccess                                       access;
        bool                                          done = false;
    };

    bool                                                  connectToTavern(int fd);
    void                                                  onNewFd(int fd, const char* token, const std::vector<const char*>& scope);
    void                                                  addClient(int fd, SAccess access, std::function<bool(std::string_view)>&& filter, bool control);

    SP<Hyprwire::IServerSocket>                           m_server;
    UP<CBarmaidServer>                                    m_barmaid;

    SP<Hyprwire::IClientSocket>                           m_tavern;
    SP<CCHpHyprtavernCoreManagerV1Object>                 m_manager;
    SP<CCHpHyprtavernBusObjectV1Object>                   m_busObject;

    std::vector<UP<SPendingConnection>>                   m_pending;
    std::unordered_map<Hyprwire::IServerClient*, SAccess> m_clients;
    CEventLoop                                            m_loop;
};

inline UP<CCore> g_core;
