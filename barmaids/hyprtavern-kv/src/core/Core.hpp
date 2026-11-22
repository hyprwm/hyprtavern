#pragma once

#include "../store/Store.hpp"

#include <common/EventLoop.hpp>
#include <common/Memory.hpp>
#include <shared/BarmaidServer.hpp>

#include <hyprtavern-client/hyprtavern-client.hpp>

#include <hyprwire/hyprwire.hpp>

#include <optional>
#include <string>

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

    bool                   init(int controlFd, int tavernFd);
    bool                   run();

    std::optional<SAccess> access(const SP<Hyprwire::IServerClient>& client) const;

    UP<Kv::CStore>         m_store;

  private:
    bool                        connectToTavern(int fd);

    SP<Hyprwire::IServerSocket> m_server;
    SP<Hyprwire::IServerClient> m_control;
    UP<CBarmaidServer>          m_barmaid;

    SP<Hyprtavern::ITavern>     m_tavern;
    SP<Hyprtavern::IBusObject>  m_busObject;
    CEventLoop                  m_loop;
};

inline UP<CCore> g_core;
