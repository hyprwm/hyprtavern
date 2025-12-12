#pragma once

#include <hyprwire/hyprwire.hpp>

#include "../helpers/Memory.hpp"

class CCoreProtocolHandler;

class CServerHandler {
  public:
    CServerHandler();
    ~CServerHandler();

    bool good();

    bool run();
    void exit();

  private:
    bool                        isAlreadyRunning();
    bool                        createLockFile();
    void                        removeFiles();

    bool                        launchBarmaids();

    bool                        m_exit = false;

    SP<Hyprwire::IServerSocket> m_socket;
};

inline UP<CServerHandler> g_serverHandler;
