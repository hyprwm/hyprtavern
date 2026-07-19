#include "ServerHandler.hpp"
#include "BarmaidProcess.hpp"
#include "ProtocolHandler.hpp"

#include "../helpers/Logger.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <future>
#include <memory>

#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <unistd.h>

#include <hyprutils/os/File.hpp>
#include <hyprutils/os/FileDescriptor.hpp>

constexpr const char* SOCKET_FILE_NAME = "ht.sock";
constexpr const char* LOCK_FILE_NAME   = ".ht-lock";

//
static std::string runtimeDir() {
    static auto ENV = getenv("XDG_RUNTIME_DIR");
    if (!ENV)
        return "";
    return ENV;
};

static void onSignal(int sig) {
    if (g_serverHandler)
        g_serverHandler->exit();
}

void CServerHandler::exit() {
    m_exit = true;
}

CServerHandler::CServerHandler() {
    signal(SIGCHLD, SIG_IGN);

    const auto RUNTIME_DIR = runtimeDir();

    if (RUNTIME_DIR.empty()) {
        g_logger->log(LOG_ERR, "XDG_RUNTIME_DIR needs to be set");
        ::exit(1);
        return;
    }

    if (isAlreadyRunning()) {
        g_logger->log(LOG_ERR, "refusing to run: hyprtavern already running for the current user");
        ::exit(1);
        return;
    }

    if (!createLockFile()) {
        g_logger->log(LOG_ERR, "refusing to run: failed to create a lock file");
        ::exit(1);
        return;
    }

    const auto      SOCK_PATH = std::filesystem::path(RUNTIME_DIR) / "hyprtavern" / SOCKET_FILE_NAME;

    std::error_code ec;
    std::filesystem::remove(SOCK_PATH, ec);

    m_socket = Hyprwire::IServerSocket::open(SOCK_PATH.string());

    if (!m_socket) {
        g_logger->log(LOG_ERR, "refusing to run: failed to open a socket");
        ::exit(1);
        return;
    }

    signal(SIGTERM, ::onSignal);
    signal(SIGINT, ::onSignal);

    g_coreProto = makeUnique<CCoreProtocolHandler>();
    if (!g_coreProto->init(m_socket)) {
        g_logger->log(LOG_ERR, "refusing to run: failed to init proto");
        ::exit(1);
        return;
    }
}

CServerHandler::~CServerHandler() {
    terminateBarmaids();
    m_socket.reset();
    removeFiles();
}

bool CServerHandler::run() {
    if (!launchBarmaids()) {
        g_logger->log(LOG_ERR, "refusing to run: failed to launch barmaids");
        return false;
    }

    int initWakeFds[2];
    if (pipe(initWakeFds) < 0) {
        g_logger->log(LOG_ERR, "failed to create barmaid init wake pipe");
        return false;
    }

    Hyprutils::OS::CFileDescriptor initWake{initWakeFds[0]}, initWakeWrite{initWakeFds[1]};
    initWake.setFlags(O_CLOEXEC);
    initWakeWrite.setFlags(O_CLOEXEC);

    constexpr size_t MAIN_FD = 0;
    constexpr size_t INIT_FD = 1;
    constexpr size_t KV_FD   = 2;
    constexpr size_t PD_FD   = 3;

    pollfd           fds[4] = {
        pollfd{
            .fd     = m_socket->extractLoopFD(),
            .events = POLLIN,
        },
        pollfd{
            .fd     = initWake.get(),
            .events = POLLIN,
        },
        pollfd{
            .revents = 0,
        },
        pollfd{
            .revents = 0,
        },
    };

    bool barmaidInitCommenced = false, barmaidInitDone = false;

    struct SBarmaidInitState {
        std::promise<bool> result;
        int                wakeFd = -1;
    };

    auto              barmaidInitState  = std::make_shared<SBarmaidInitState>();
    std::future<bool> barmaidInitFuture = barmaidInitState->result.get_future();

    auto              drainWakeFd = [&initWake] {
        char buf[128];
        while (initWake.isValid() && initWake.isReadable()) {
            const auto BYTES = read(initWake.get(), buf, sizeof(buf));
            if (BYTES <= 0)
                break;
        }
    };

    auto processBarmaidInitResult = [&]() -> bool {
        if (barmaidInitDone || !barmaidInitCommenced || barmaidInitFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return true;

        drainWakeFd();

        barmaidInitDone = true;
        if (!barmaidInitFuture.get()) {
            g_logger->log(LOG_ERR, "barmaid init failed");
            exit();
            return false;
        }

        fds[KV_FD].fd     = g_coreProto->m_client.kvSock->extractLoopFD();
        fds[KV_FD].events = POLLIN;

        fds[PD_FD].fd     = g_coreProto->m_client.pdSock->extractLoopFD();
        fds[PD_FD].events = POLLIN;

        g_logger->log(LOG_DEBUG, "barmaid init finished");
        g_coreProto->sendReady();
        return true;
    };

    while (!m_exit) {
        const nfds_t NFDS = barmaidInitDone ? 4 : (barmaidInitCommenced ? 2 : 1);

        if (poll(fds, NFDS, -1) < 0) {
            g_logger->log(LOG_ERR, "poll() failed");
            exit();
            return false;
        }

        // TODO: restrict new clients connecting until barmaids are init'd

        if (barmaidInitCommenced && fds[INIT_FD].revents & POLLIN)
            drainWakeFd();

        if (!processBarmaidInitResult())
            return false;

        if (barmaidInitCommenced && !barmaidInitDone && fds[INIT_FD].revents & (POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "barmaid init wake fd died");
            exit();
            return false;
        }

        if (fds[MAIN_FD].revents & POLLIN) {
            if (!m_socket->dispatchEvents()) {
                g_logger->log(LOG_ERR, "socket fd dispatch failed");
                exit();
                return false;
            }

            if (barmaidInitDone)
                g_coreProto->sendReady();
        }

        if (barmaidInitDone && fds[KV_FD].revents & POLLIN) {
            if (!g_coreProto->m_client.kvSock->dispatchEvents()) {
                g_logger->log(LOG_ERR, "kv fd dispatch failed");
                exit();
                return false;
            }
        }

        if (barmaidInitDone && fds[PD_FD].revents & POLLIN) {
            if (!g_coreProto->m_client.pdSock->dispatchEvents()) {
                g_logger->log(LOG_ERR, "pd fd dispatch failed");
                exit();
                return false;
            }
        }

        // TODO: this should be done better
        if (!barmaidInitCommenced && g_coreProto->m_managers.size() >= 2 /* kv_store and pd */) {
            barmaidInitCommenced = true;

            barmaidInitState->wakeFd = dup(initWakeWrite.get());
            if (barmaidInitState->wakeFd < 0) {
                g_logger->log(LOG_ERR, "failed to duplicate barmaid init wake fd");
                exit();
                return false;
            }

            std::thread t([state = barmaidInitState] {
                state->result.set_value(g_coreProto->initBarmaids());
                if (state->wakeFd >= 0) {
                    const auto WRITTEN = write(state->wakeFd, "x", 1);
                    (void)WRITTEN;
                    close(state->wakeFd);
                }
            });
            t.detach();
        }

        if (!processBarmaidInitResult())
            return false;

        if (fds[MAIN_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "socket fd died");
            return true;
        }

        if (barmaidInitDone && fds[KV_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "kv fd died");
            exit();
            return false;
        }

        if (barmaidInitDone && fds[PD_FD].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            g_logger->log(LOG_ERR, "pd fd died");
            exit();
            return false;
        }
    }

    return true;
}

bool CServerHandler::isAlreadyRunning() {
    const std::filesystem::path RUNTIME_DIR = runtimeDir();

    std::error_code             ec;
    if (!std::filesystem::exists(RUNTIME_DIR / "hyprtavern", ec) || ec)
        return false;

    if (!std::filesystem::exists(RUNTIME_DIR / "hyprtavern/.ht-lock", ec) || ec)
        return false;

    const auto FILE_CONTENT = Hyprutils::File::readFileAsString((RUNTIME_DIR / "hyprtavern" / LOCK_FILE_NAME).string());

    if (!FILE_CONTENT) {
        g_logger->log(LOG_ERR, "Refusing to continue: lockfile exists but inaccessible: error {}", FILE_CONTENT.error());
        ::exit(1);
    }

    try {
        int pid = std::stoi(*FILE_CONTENT);
        if (::kill(pid, 0) == 0)
            return true;

        if (errno == EPERM)
            return true;

        return false;
    } catch (...) {
        g_logger->log(LOG_ERR, "Refusing to continue: lockfile corrupt");
        ::exit(1);
    }

    return false; // unreachable
}

bool CServerHandler::createLockFile() {
    const std::filesystem::path RUNTIME_DIR = runtimeDir();

    std::error_code             ec;
    if (!std::filesystem::exists(RUNTIME_DIR / "hyprtavern", ec) || ec) {
        std::filesystem::create_directory(RUNTIME_DIR / "hyprtavern", ec);

        if (ec) {
            g_logger->log(LOG_ERR, "Failed to create the lockfile dir at {}: {}", (RUNTIME_DIR / "hyprtavern").string(), ec.message());
            return false;
        }
    }

    std::ofstream ofs(RUNTIME_DIR / "hyprtavern" / LOCK_FILE_NAME, std::ios::trunc);
    if (!ofs.good()) {
        g_logger->log(LOG_ERR, "Failed to open a lockfile at {}", (RUNTIME_DIR / "hyprtavern" / LOCK_FILE_NAME).string());
        return false;
    }

    ofs << getpid() << std::endl;
    ofs.close();

    return true;
}

void CServerHandler::removeFiles() {
    const std::filesystem::path RUNTIME_DIR = runtimeDir();

    std::error_code             ec;
    std::filesystem::remove(RUNTIME_DIR / "hyprtavern" / LOCK_FILE_NAME, ec);

    if (ec)
        g_logger->log(LOG_ERR, "failed to remove lock file");

    std::filesystem::remove(RUNTIME_DIR / "hyprtavern" / SOCKET_FILE_NAME, ec);

    if (ec)
        g_logger->log(LOG_ERR, "failed to remove socket file");
}

void CServerHandler::terminateBarmaids() {
    for (const auto& pid : m_barmaidPids) {
        CBarmaidProcess::terminate(pid);
    }

    m_barmaidPids.clear();
}

bool CServerHandler::good() {
    return !!m_socket;
}

bool CServerHandler::launchBarmaids() {

    // ----------------- KV ----------------- //
    {
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            g_logger->log(LOG_ERR, "failed to create a socketpair");
            return false;
        }

        fcntl(fds[0], F_SETFD, FD_CLOEXEC);

        auto pid = CBarmaidProcess::launch("hyprtavern-kv", {"--fd", std::format("{}", fds[1])});

        close(fds[1]);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!CBarmaidProcess::isRunning(pid)) {
            close(fds[0]);
            terminateBarmaids();
            return false;
        }

        m_barmaidPids.emplace_back(pid);

        auto client = m_socket->addClient(fds[0]);
        if (!client) {
            close(fds[0]);
            terminateBarmaids();
            return false;
        }

        g_coreProto->registerInternalClient(client);

        g_logger->log(LOG_DEBUG, "hyprtavern-kv started");
    }

    // ----------------- PD ----------------- //
    {
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            g_logger->log(LOG_ERR, "failed to create a socketpair");
            terminateBarmaids();
            return false;
        }

        fcntl(fds[0], F_SETFD, FD_CLOEXEC);

        auto pid = CBarmaidProcess::launch("hyprtavern-perm-daemon", {"--fd", std::format("{}", fds[1])});

        close(fds[1]);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!CBarmaidProcess::isRunning(pid)) {
            close(fds[0]);
            terminateBarmaids();
            return false;
        }

        m_barmaidPids.emplace_back(pid);

        auto client = m_socket->addClient(fds[0]);
        if (!client) {
            close(fds[0]);
            terminateBarmaids();
            return false;
        }

        g_coreProto->registerInternalClient(client);

        g_logger->log(LOG_DEBUG, "hyprtavern-pd started");
    }

    return true;
}
