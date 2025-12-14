#include "ServerHandler.hpp"
#include "ProtocolHandler.hpp"

#include "../helpers/Logger.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <future>

#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/poll.h>

#include <hyprutils/os/File.hpp>

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
    m_socket.reset();
    removeFiles();
}

bool CServerHandler::run() {
    if (!launchBarmaids()) {
        g_logger->log(LOG_ERR, "refusing to run: failed to launch barmaids");
        return false;
    }

    pollfd fds[2] = {
        pollfd{
            .fd     = m_socket->extractLoopFD(),
            .events = POLLIN,
        },
        pollfd{
            .revents = 0,
        },
    };

    bool               barmaidInitCommenced = false, barmaidInitDone = false;
    std::promise<bool> barmaidInitResult;
    std::future<bool>  barmaidInitFuture = barmaidInitResult.get_future();

    while (!m_exit) {
        if (poll(fds, barmaidInitDone ? 2 : 1, -1) < 0) {
            g_logger->log(LOG_ERR, "poll() failed");
            exit();
            return false;
        }

        // TODO: restrict new clients connecting until barmaids are init'd

        if (fds[0].revents & POLLIN)
            m_socket->dispatchEvents();
        if (fds[1].revents & POLLIN)
            g_coreProto->m_client.kvSock->dispatchEvents();

        if (!barmaidInitCommenced && g_coreProto->m_managers.size() >= 1 /* kv_store */) {
            barmaidInitCommenced = true;
            std::thread t([&barmaidInitResult] { barmaidInitResult.set_value(g_coreProto->initBarmaids()); });
            t.detach();
        }

        if (!barmaidInitDone && barmaidInitFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            barmaidInitDone = true;
            if (!barmaidInitFuture.get()) {
                g_logger->log(LOG_ERR, "barmaid init failed");
                exit();
                return false;
            }

            fds[1].fd     = g_coreProto->m_client.kvSock->extractLoopFD();
            fds[1].events = POLLIN;
        }

        if (fds[0].revents & POLLHUP) {
            g_logger->log(LOG_ERR, "socket fd died");
            return true;
        }

        if (fds[1].revents & POLLHUP) {
            g_logger->log(LOG_ERR, "tavernkeep fd died");
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

bool CServerHandler::good() {
    return m_socket;
}

static pid_t launch(const std::string& app, const std::vector<std::string>& params) {
    std::vector<const char*> argv = {app.c_str()};
    argv.reserve(params.size() + 1);
    for (const auto& p : params) {
        argv.emplace_back(p.c_str());
    }
    argv.emplace_back(nullptr);

    auto fk = fork();

    if (fk < 0) {
        g_logger->log(LOG_ERR, "failed to fork for exec {}", app);
        return fk;
    }

    if (fk == 0) {
        execvp(app.c_str(), cc<char* const*>(argv.data()));
        g_logger->log(LOG_ERR, "failed to execv {}", app);
        exit(1);
    }

    return fk;
}

static bool isRunning(pid_t pid) {
    if (pid <= 0)
        return false;

    if (::kill(pid, 0) == 0)
        return true;

    if (errno == EPERM)
        return true;

    return false;
}

bool CServerHandler::launchBarmaids() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        g_logger->log(LOG_ERR, "failed to create a socketpair");
        return false;
    }

    fcntl(fds[0], F_SETFD, FD_CLOEXEC);

    auto pid = launch("hyprtavern-kv", {"--fd", std::format("{}", fds[1])});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (!isRunning(pid))
        return false;

    m_socket->addClient(fds[0]);

    return true;
}
