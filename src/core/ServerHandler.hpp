#pragma once

#include <hyprwire/hyprwire.hpp>

#include "../helpers/Memory.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <vector>

#include <signal.h>
#include <sys/types.h>

class CCoreProtocolHandler;

class CServerHandler {
  public:
    CServerHandler();
    ~CServerHandler();

    bool good() const;

    bool run();
    void exit();

  private:
    class COwnedFD {
      public:
        COwnedFD() = default;
        explicit COwnedFD(int fd);
        ~COwnedFD();

        COwnedFD(const COwnedFD&)            = delete;
        COwnedFD& operator=(const COwnedFD&) = delete;

        COwnedFD(COwnedFD&& other) noexcept;
        COwnedFD& operator=(COwnedFD&& other) noexcept;

        int       get() const;
        int       release();
        void      reset(int fd = -1);
        bool      valid() const;

      private:
        int m_fd = -1;
    };

    bool                            acquireRuntimeLock();
    bool                            setupLifecyclePipe();
    bool                            installSignalHandlers();
    void                            restoreSignalHandlers();
    void                            removeFiles();
    void                            wakeEventLoop() const;
    void                            drainLifecyclePipe();
    size_t                          reapBarmaids();
    void                            terminateBarmaids();

    bool                            launchBarmaids();

    std::atomic_bool                m_exit         = false;
    bool                            m_good         = false;
    bool                            m_childFailure = false;

    std::filesystem::path           m_runtimeDir;
    std::filesystem::path           m_lockPath;
    std::filesystem::path           m_socketPath;

    COwnedFD                        m_lockFd;
    COwnedFD                        m_lifecycleReadFd;
    COwnedFD                        m_lifecycleWriteFd;

    std::array<struct sigaction, 3> m_previousSignalActions{};
    bool                            m_signalHandlersInstalled = false;

    SP<Hyprwire::IServerSocket>     m_socket;
    std::vector<pid_t>              m_barmaidPids;
};

inline UP<CServerHandler> g_serverHandler;
