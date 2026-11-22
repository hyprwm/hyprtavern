#pragma once

#include "helpers/EventLoop.hpp"

#include <common/Memory.hpp>

#include <hyprutils/os/FileDescriptor.hpp>
#include <hyprwire/hyprwire.hpp>

#include <filesystem>

struct STavernOptions {
    bool enforcePermissions = false;
};

// owns the bus socket, the event loop and the lifetime of everything else
class CTavern {
  public:
    CTavern(const STavernOptions& options);
    ~CTavern();

    bool good() const;

    // runs until exit() is called. Returns false on failure.
    bool run();
    void exit(bool success);

  private:
    bool                           initRuntimeDir();
    bool                           initSignals();
    void                           onSignals();

    bool                           m_good    = false;
    bool                           m_success = true;

    std::filesystem::path          m_socketPath;
    Hyprutils::OS::CFileDescriptor m_lockFd;
    Hyprutils::OS::CFileDescriptor m_signalReadFd, m_signalWriteFd;

    SP<Hyprwire::IServerSocket>    m_socket;
    CEventLoop                     m_loop;
};

inline UP<CTavern> g_tavern;
