#pragma once

#include <common/Memory.hpp>

#include <hp_hyprtavern_barmaid_v1-server.hpp>

#include <hyprutils/signal/Signal.hpp>

#include <vector>

// serves hp_hyprtavern_barmaid_v1 on a barmaid's control channel. Only the tavern may bind it.
class CBarmaidServer {
  public:
    CBarmaidServer(const SP<Hyprwire::IServerSocket>& socket);
    ~CBarmaidServer();

    // tells the tavern this barmaid can serve
    void setReady();

    struct {
        // after an environment update was applied to this process
        Hyprutils::Signal::CSignalT<> environmentChanged;
    } m_events;

  private:
    void                                                 bindManager(SP<Hyprwire::IObject> object);

    std::vector<SP<CHpHyprtavernBarmaidManagerV1Object>> m_managers;
    bool                                                 m_ready = false;
};
