#include "ServerHandler.hpp"
#include "ProtocolHandler.hpp"

#include "../helpers/Logger.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>

#include <sys/signal.h>

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
    const auto RUNTIME_DIR = runtimeDir();

    if (RUNTIME_DIR.empty()) {
        g_logger->log(LOG_ERR, "XDG_RUNTIME_DIR needs to be set");
        return;
    }

    if (isAlreadyRunning()) {
        g_logger->log(LOG_ERR, "refusing to run: hyprtavern already running for the current user");
        return;
    }

    if (!createLockFile()) {
        g_logger->log(LOG_ERR, "refusing to run: failed to create a lock file");
        return;
    }

    m_socket = Hyprwire::IServerSocket::open((std::filesystem::path(RUNTIME_DIR) / "hyprtavern" / SOCKET_FILE_NAME).string());

    if (!m_socket) {
        g_logger->log(LOG_ERR, "refusing to run: failed to open a socket");
        return;
    }

    signal(SIGTERM, ::onSignal);
    signal(SIGINT, ::onSignal);

    g_coreProto = makeUnique<CCoreProtocolHandler>();
    g_coreProto->init(m_socket);
}

CServerHandler::~CServerHandler() {
    m_socket.reset();
    removeFiles();
}

bool CServerHandler::run() {
    while (!m_exit) {
        m_socket->dispatchEvents(true);
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