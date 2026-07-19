#pragma once

#include <hyprwire/hyprwire.hpp>

#include "../helpers/Memory.hpp"

#include <sys/types.h>
#include <vector>

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
    void                        terminateBarmaids();

    bool                        launchBarmaids();

    bool                        m_exit = false;

    SP<Hyprwire::IServerSocket> m_socket;
    std::vector<pid_t>          m_barmaidPids;
};

inline UP<CServerHandler> g_serverHandler;
