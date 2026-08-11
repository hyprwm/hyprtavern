#include "ServerHandler.hpp"

#include "BarmaidConnector.hpp"
#include "BarmaidProcess.hpp"
#include "ProtocolHandler.hpp"

#include "../helpers/Logger.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <future>
#include <string>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
    constexpr const char*        SOCKET_FILE_NAME = "ht.sock";
    constexpr const char*        LOCK_FILE_NAME   = ".ht-lock";
    constexpr std::array<int, 3> HANDLED_SIGNALS  = {SIGTERM, SIGINT, SIGCHLD};

    volatile sig_atomic_t        g_signalWriteFd        = -1;
    volatile sig_atomic_t        g_terminationRequested = 0;
    volatile sig_atomic_t        g_childChanged         = 0;

    void                         onLifecycleSignal(int signal) {
        const int SAVED_ERRNO = errno;

        if (signal == SIGCHLD)
            g_childChanged = 1;
        else
            g_terminationRequested = 1;

        const int fd = static_cast<int>(g_signalWriteFd);
        if (fd >= 0) {
            const unsigned char value = static_cast<unsigned char>(signal);
            const ssize_t       ret   = write(fd, &value, sizeof(value));
            (void)ret;
        }

        errno = SAVED_ERRNO;
    }

    bool setCloseOnExec(int fd) {
        const int FLAGS = fcntl(fd, F_GETFD);
        return FLAGS >= 0 && fcntl(fd, F_SETFD, FLAGS | FD_CLOEXEC) >= 0;
    }

    bool setNonBlocking(int fd) {
        const int FLAGS = fcntl(fd, F_GETFL);
        return FLAGS >= 0 && fcntl(fd, F_SETFL, FLAGS | O_NONBLOCK) >= 0;
    }

    bool writeAll(int fd, const std::string& data) {
        size_t written = 0;
        while (written < data.size()) {
            const ssize_t ret = write(fd, data.data() + written, data.size() - written);
            if (ret > 0) {
                written += static_cast<size_t>(ret);
                continue;
            }
            if (ret < 0 && errno == EINTR)
                continue;
            return false;
        }
        return true;
    }

    void writeWakeByte(int fd) {
        if (fd < 0)
            return;

        const unsigned char value = 0;
        while (write(fd, &value, sizeof(value)) < 0 && errno == EINTR) {}
    }

    void drainFd(int fd) {
        char buffer[128];
        while (fd >= 0) {
            const ssize_t bytes = read(fd, buffer, sizeof(buffer));
            if (bytes > 0)
                continue;
            if (bytes < 0 && errno == EINTR)
                continue;
            break;
        }
    }
}

CServerHandler::COwnedFD::COwnedFD(int fd) : m_fd(fd) {}

CServerHandler::COwnedFD::~COwnedFD() {
    reset();
}

CServerHandler::COwnedFD::COwnedFD(COwnedFD&& other) noexcept : m_fd(other.release()) {}

CServerHandler::COwnedFD& CServerHandler::COwnedFD::operator=(COwnedFD&& other) noexcept {
    if (this != &other)
        reset(other.release());
    return *this;
}

int CServerHandler::COwnedFD::get() const {
    return m_fd;
}

int CServerHandler::COwnedFD::release() {
    const int fd = m_fd;
    m_fd         = -1;
    return fd;
}

void CServerHandler::COwnedFD::reset(int fd) {
    if (m_fd >= 0)
        close(m_fd);
    m_fd = fd;
}

bool CServerHandler::COwnedFD::valid() const {
    return m_fd >= 0;
}

CServerHandler::CServerHandler() {
    const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir || !*runtimeDir) {
        g_logger->log(LOG_ERR, "XDG_RUNTIME_DIR needs to be set");
        return;
    }

    m_runtimeDir = std::filesystem::path{std::string{runtimeDir}} / "hyprtavern";
    m_lockPath   = m_runtimeDir / LOCK_FILE_NAME;
    m_socketPath = m_runtimeDir / SOCKET_FILE_NAME;

    if (!acquireRuntimeLock())
        return;

    if (!setupLifecyclePipe() || !installSignalHandlers())
        return;

    std::error_code ec;
    std::filesystem::remove(m_socketPath, ec);
    if (ec) {
        g_logger->log(LOG_ERR, "failed to remove stale socket at {}: {}", m_socketPath.string(), ec.message());
        return;
    }

    m_socket = Hyprwire::IServerSocket::open(m_socketPath.string());
    if (!m_socket) {
        g_logger->log(LOG_ERR, "refusing to run: failed to open socket at {}", m_socketPath.string());
        return;
    }

    g_coreProto = makeUnique<CCoreProtocolHandler>();
    if (!g_coreProto->init(m_socket)) {
        g_logger->log(LOG_ERR, "refusing to run: failed to init proto");
        m_socket.reset();
        return;
    }

    m_good = true;
}

CServerHandler::~CServerHandler() {
    m_good = false;
    m_exit.store(true, std::memory_order_relaxed);
    terminateBarmaids();
    m_socket.reset();
    removeFiles();
    restoreSignalHandlers();
    m_lifecycleWriteFd.reset();
    m_lifecycleReadFd.reset();
    m_lockFd.reset();
}

bool CServerHandler::acquireRuntimeLock() {
    std::error_code ec;
    if (!std::filesystem::exists(m_runtimeDir, ec)) {
        ec.clear();
        const bool CREATED = std::filesystem::create_directory(m_runtimeDir, ec);
        if (ec || (!CREATED && !std::filesystem::is_directory(m_runtimeDir, ec)) || ec) {
            g_logger->log(LOG_ERR, "failed to create runtime directory at {}: {}", m_runtimeDir.string(), ec.message());
            return false;
        }
    } else if (ec || !std::filesystem::is_directory(m_runtimeDir, ec) || ec) {
        g_logger->log(LOG_ERR, "runtime path is not an accessible directory: {}", m_runtimeDir.string());
        return false;
    }

    COwnedFD lockFd{open(m_lockPath.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)};
    if (!lockFd.valid()) {
        g_logger->log(LOG_ERR, "failed to open lock file at {}: {}", m_lockPath.string(), std::strerror(errno));
        return false;
    }

    if (!setCloseOnExec(lockFd.get())) {
        g_logger->log(LOG_ERR, "failed to set lock file close-on-exec: {}", std::strerror(errno));
        return false;
    }

    if (flock(lockFd.get(), LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            g_logger->log(LOG_ERR, "refusing to run: hyprtavern is already running for the current user");
        else
            g_logger->log(LOG_ERR, "failed to lock {}: {}", m_lockPath.string(), std::strerror(errno));
        return false;
    }

    if (ftruncate(lockFd.get(), 0) < 0 || lseek(lockFd.get(), 0, SEEK_SET) < 0 || !writeAll(lockFd.get(), std::format("{}\n", getpid()))) {
        g_logger->log(LOG_ERR, "failed to write lock file at {}: {}", m_lockPath.string(), std::strerror(errno));
        return false;
    }

    m_lockFd = std::move(lockFd);
    return true;
}

bool CServerHandler::setupLifecyclePipe() {
    int fds[2] = {-1, -1};
    if (pipe(fds) < 0) {
        g_logger->log(LOG_ERR, "failed to create lifecycle self-pipe: {}", std::strerror(errno));
        return false;
    }

    COwnedFD readFd{fds[0]};
    COwnedFD writeFd{fds[1]};
    if (!setCloseOnExec(readFd.get()) || !setCloseOnExec(writeFd.get()) || !setNonBlocking(readFd.get()) || !setNonBlocking(writeFd.get())) {
        g_logger->log(LOG_ERR, "failed to configure lifecycle self-pipe: {}", std::strerror(errno));
        return false;
    }

    m_lifecycleReadFd  = std::move(readFd);
    m_lifecycleWriteFd = std::move(writeFd);
    return true;
}

bool CServerHandler::installSignalHandlers() {
    struct sigaction action{};
    sigemptyset(&action.sa_mask);
    action.sa_handler = onLifecycleSignal;
    action.sa_flags   = 0;

    g_terminationRequested = 0;
    g_childChanged         = 0;
    g_signalWriteFd        = static_cast<sig_atomic_t>(m_lifecycleWriteFd.get());

    size_t installed = 0;
    for (; installed < HANDLED_SIGNALS.size(); ++installed) {
        if (sigaction(HANDLED_SIGNALS[installed], &action, &m_previousSignalActions[installed]) == 0)
            continue;

        const int SIGNAL_ERRNO = errno;
        while (installed > 0) {
            --installed;
            sigaction(HANDLED_SIGNALS[installed], &m_previousSignalActions[installed], nullptr);
        }
        g_signalWriteFd = -1;
        errno           = SIGNAL_ERRNO;
        g_logger->log(LOG_ERR, "failed to install lifecycle signal handlers: {}", std::strerror(errno));
        return false;
    }

    m_signalHandlersInstalled = true;
    return true;
}

void CServerHandler::restoreSignalHandlers() {
    if (!m_signalHandlersInstalled)
        return;

    g_signalWriteFd = -1;
    for (size_t i = 0; i < HANDLED_SIGNALS.size(); ++i) {
        if (sigaction(HANDLED_SIGNALS[i], &m_previousSignalActions[i], nullptr) < 0)
            g_logger->log(LOG_WARN, "failed to restore signal {} handler: {}", HANDLED_SIGNALS[i], std::strerror(errno));
    }
    m_signalHandlersInstalled = false;
}

void CServerHandler::exit() {
    m_exit.store(true, std::memory_order_relaxed);
    wakeEventLoop();
}

void CServerHandler::wakeEventLoop() const {
    writeWakeByte(m_lifecycleWriteFd.get());
}

void CServerHandler::drainLifecyclePipe() {
    drainFd(m_lifecycleReadFd.get());

    if (g_terminationRequested) {
        g_terminationRequested = 0;
        m_exit.store(true, std::memory_order_relaxed);
    }

    if (g_childChanged) {
        g_childChanged      = 0;
        const size_t REAPED = reapBarmaids();
        if (REAPED > 0 && !m_exit.load(std::memory_order_relaxed)) {
            g_logger->log(LOG_ERR, "a barmaid exited unexpectedly");
            m_childFailure = true;
            m_exit.store(true, std::memory_order_relaxed);
        }
    }
}

bool CServerHandler::run() {
    if (!m_good || !m_socket || !g_coreProto)
        return false;

    if (!launchBarmaids()) {
        g_logger->log(LOG_ERR, "refusing to run: failed to launch barmaids");
        return false;
    }

    int initWakeFds[2] = {-1, -1};
    if (pipe(initWakeFds) < 0) {
        g_logger->log(LOG_ERR, "failed to create barmaid init wake pipe: {}", std::strerror(errno));
        terminateBarmaids();
        return false;
    }

    COwnedFD initWakeRead{initWakeFds[0]};
    COwnedFD initWakeWrite{initWakeFds[1]};
    if (!setCloseOnExec(initWakeRead.get()) || !setCloseOnExec(initWakeWrite.get()) || !setNonBlocking(initWakeRead.get()) || !setNonBlocking(initWakeWrite.get())) {
        g_logger->log(LOG_ERR, "failed to configure barmaid init wake pipe: {}", std::strerror(errno));
        terminateBarmaids();
        return false;
    }

    constexpr size_t      MAIN_FD      = 0;
    constexpr size_t      LIFECYCLE_FD = 1;
    constexpr size_t      INIT_FD      = 2;
    constexpr size_t      KV_FD        = 3;
    constexpr size_t      PD_FD        = 4;

    std::array<pollfd, 5> fds = {
        pollfd{.fd = m_socket->extractLoopFD(), .events = POLLIN, .revents = 0},
        pollfd{.fd = m_lifecycleReadFd.get(), .events = POLLIN, .revents = 0},
        pollfd{.fd = initWakeRead.get(), .events = POLLIN, .revents = 0},
        pollfd{.fd = -1, .events = POLLIN, .revents = 0},
        pollfd{.fd = -1, .events = POLLIN, .revents = 0},
    };

    constexpr auto     STARTUP_TIMEOUT    = std::chrono::seconds(30);
    const auto         STARTUP_DEADLINE   = std::chrono::steady_clock::now() + STARTUP_TIMEOUT;
    bool               success            = true;
    bool               barmaidInitStarted = false;
    bool               barmaidInitDone    = false;
    std::promise<bool> initPromise;
    std::future<bool>  initFuture = initPromise.get_future();
    std::jthread       initThread;

    auto               processInitResult = [&]() {
        if (!barmaidInitStarted || barmaidInitDone || initFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        const bool INIT_OK = initFuture.get();
        if (initThread.joinable())
            initThread.join();
        barmaidInitDone = true;
        drainFd(initWakeRead.get());

        if (!INIT_OK || !g_coreProto->m_client.kvSock || !g_coreProto->m_client.pdSock) {
            g_logger->log(LOG_ERR, "barmaid init failed");
            success = false;
            m_exit.store(true, std::memory_order_relaxed);
            return;
        }

        // The future and join publish all m_client writes before readiness is exposed.
        g_coreProto->m_barmaidsReady = true;
        fds[KV_FD].fd                = g_coreProto->m_client.kvSock->extractLoopFD();
        fds[PD_FD].fd                = g_coreProto->m_client.pdSock->extractLoopFD();

        g_logger->log(LOG_DEBUG, "barmaid init finished");
        g_coreProto->sendReady();
    };

    while (!m_exit.load(std::memory_order_relaxed)) {
        processInitResult();
        if (m_exit.load(std::memory_order_relaxed))
            break;

        if (!barmaidInitDone && std::chrono::steady_clock::now() >= STARTUP_DEADLINE) {
            g_logger->log(LOG_ERR, "barmaid startup timed out");
            success = false;
            break;
        }

        const int TIMEOUT = !barmaidInitDone ? 250 : -1;
        const int RET     = poll(fds.data(), static_cast<nfds_t>(fds.size()), TIMEOUT);
        if (RET < 0) {
            if (errno == EINTR) {
                drainLifecyclePipe();
                continue;
            }
            g_logger->log(LOG_ERR, "poll() failed: {}", std::strerror(errno));
            success = false;
            break;
        }

        if (fds[LIFECYCLE_FD].revents & POLLIN)
            drainLifecyclePipe();
        if (fds[LIFECYCLE_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "lifecycle self-pipe died");
            success = false;
            break;
        }
        if (m_exit.load(std::memory_order_relaxed))
            break;

        if (fds[INIT_FD].revents & POLLIN)
            drainFd(initWakeRead.get());
        if (fds[INIT_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "barmaid init wake fd died");
            success = false;
            break;
        }

        processInitResult();
        if (m_exit.load(std::memory_order_relaxed))
            break;

        if (fds[MAIN_FD].revents & POLLIN) {
            if (!m_socket->dispatchEvents()) {
                g_logger->log(LOG_ERR, "socket fd dispatch failed");
                success = false;
                break;
            }

            if (barmaidInitDone)
                g_coreProto->sendReady();
        }

        if (barmaidInitDone && fds[KV_FD].revents & POLLIN) {
            if (!g_coreProto->m_client.kvSock->dispatchEvents()) {
                g_logger->log(LOG_ERR, "kv fd dispatch failed");
                success = false;
                break;
            }
        }

        if (barmaidInitDone && fds[PD_FD].revents & POLLIN) {
            if (!g_coreProto->m_client.pdSock->dispatchEvents()) {
                g_logger->log(LOG_ERR, "pd fd dispatch failed");
                success = false;
                break;
            }
        }

        const auto internalClientCount = std::ranges::count_if(g_coreProto->m_internalClients, [](const auto& client) { return static_cast<bool>(client); });
        if (!barmaidInitStarted && internalClientCount >= 3) { // Internal core client plus both launched barmaids.
            barmaidInitStarted = true;
            initThread         = std::jthread([&initPromise, wakeFd = initWakeWrite.get()](std::stop_token stopToken) {
                CBarmaidConnector::setThreadStopToken(stopToken);

                bool result = false;
                try {
                    result = !stopToken.stop_requested() && g_coreProto->initKv();
                    result = result && !stopToken.stop_requested() && g_coreProto->initPd();
                    result = result && !stopToken.stop_requested();
                    initPromise.set_value(result);
                } catch (...) {
                    try {
                        initPromise.set_value(false);
                    } catch (...) {}
                }

                CBarmaidConnector::setThreadStopToken({});
                writeWakeByte(wakeFd);
            });
        }

        processInitResult();
        if (m_exit.load(std::memory_order_relaxed))
            break;

        if (fds[MAIN_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "socket fd died");
            break;
        }

        if (barmaidInitDone && fds[KV_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "kv fd died");
            success = false;
            break;
        }

        if (barmaidInitDone && fds[PD_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "pd fd died");
            success = false;
            break;
        }
    }

    if (initThread.joinable()) {
        initThread.request_stop();
        if (!barmaidInitDone) {
            if (g_coreProto->m_client.sock)
                shutdown(g_coreProto->m_client.sock->extractLoopFD(), SHUT_RDWR);
            terminateBarmaids();
        }
        initThread.join();
    }

    return success && !m_childFailure;
}

void CServerHandler::removeFiles() {
    if (m_socketPath.empty())
        return;

    std::error_code ec;
    std::filesystem::remove(m_socketPath, ec);
    if (ec)
        g_logger->log(LOG_ERR, "failed to remove socket file at {}: {}", m_socketPath.string(), ec.message());

    // The lock file intentionally persists: unlinking a flock file permits a second inode
    // to be locked before this process has released the original lock.
}

size_t CServerHandler::reapBarmaids() {
    size_t reaped = 0;

    std::erase_if(m_barmaidPids, [&reaped](pid_t pid) {
        int   status = 0;
        pid_t ret    = -1;
        do {
            ret = waitpid(pid, &status, WNOHANG);
        } while (ret < 0 && errno == EINTR);

        if (ret == 0)
            return false;
        if (ret == pid) {
            ++reaped;
            if (WIFEXITED(status))
                g_logger->log(LOG_WARN, "barmaid pid {} exited with status {}", pid, WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                g_logger->log(LOG_WARN, "barmaid pid {} exited from signal {}", pid, WTERMSIG(status));
            return true;
        }
        if (ret < 0 && errno != ECHILD)
            g_logger->log(LOG_WARN, "failed to inspect barmaid pid {}: {}", pid, std::strerror(errno));
        return ret < 0 && errno == ECHILD;
    });

    return reaped;
}

void CServerHandler::terminateBarmaids() {
    for (const pid_t pid : m_barmaidPids)
        CBarmaidProcess::terminate(pid);
    m_barmaidPids.clear();
}

bool CServerHandler::good() const {
    return m_good && !!m_socket;
}

bool CServerHandler::launchBarmaids() {
    auto launch = [this](const std::string& app, const char* description) {
        int fds[2] = {-1, -1};
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            g_logger->log(LOG_ERR, "failed to create socketpair for {}: {}", app, std::strerror(errno));
            return false;
        }

        COwnedFD serverFd{fds[0]};
        COwnedFD childFd{fds[1]};
        if (!setCloseOnExec(serverFd.get())) {
            g_logger->log(LOG_ERR, "failed to set parent socket close-on-exec for {}: {}", app, std::strerror(errno));
            return false;
        }

        const pid_t pid = CBarmaidProcess::launch(app, {"--fd", std::format("{}", childFd.get())});
        childFd.reset();
        if (pid <= 0)
            return false;

        m_barmaidPids.emplace_back(pid);

        const int clientFd = serverFd.release();
        auto      client   = m_socket->addClient(clientFd);
        if (!client) {
            close(clientFd);
            return false;
        }

        g_coreProto->registerInternalClient(client);
        g_logger->log(LOG_DEBUG, "{} started with pid {}", description, pid);
        return true;
    };

    if (!launch("hyprtavern-kv", "hyprtavern-kv")) {
        terminateBarmaids();
        return false;
    }

    if (!launch("hyprtavern-perm-daemon", "hyprtavern-pd")) {
        terminateBarmaids();
        return false;
    }

    return true;
}
