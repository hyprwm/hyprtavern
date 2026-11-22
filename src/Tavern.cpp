#include "Tavern.hpp"
#include "barmaids/KvBarmaid.hpp"
#include "barmaids/PermissionBarmaid.hpp"
#include "bus/PrincipalTokens.hpp"
#include "bus/Registry.hpp"
#include "core/ClientManager.hpp"
#include "protocols/core/CoreProtocol.hpp"

#include <common/Environment.hpp>
#include <common/Log.hpp>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

using namespace Hyprutils::OS;

namespace {
    constexpr const char*        SOCKET_FILE_NAME = "ht.sock";
    constexpr const char*        LOCK_FILE_NAME   = ".ht-lock";
    constexpr std::array<int, 3> HANDLED_SIGNALS  = {SIGTERM, SIGINT, SIGCHLD};
    constexpr auto               STARTUP_TIMEOUT  = std::chrono::seconds{30};

    volatile sig_atomic_t        g_signalFd = -1;

    void                         onSignal(int signal) {
        const int     SAVED_ERRNO = errno;
        const uint8_t BYTE        = signal;

        sc<void>(write(g_signalFd, &BYTE, 1));

        errno = SAVED_ERRNO;
    }
}

CTavern::CTavern(const STavernOptions& options) {
    if (!initRuntimeDir() || !initSignals())
        return;

    m_socket = Hyprwire::IServerSocket::open(m_socketPath.string());
    if (!m_socket) {
        g_logger->log(LOG_ERR, "failed to open the bus socket at {}", m_socketPath.string());
        return;
    }

    Bus::registry = makeUnique<Bus::CRegistry>();
    Bus::tokens   = makeUnique<Bus::CPrincipalTokens>();
    Core::clients = makeUnique<Core::CClientManager>(options.enforcePermissions);
    PROTO::core   = makeUnique<CCoreProtocol>(m_socket);

    m_socket->setNewClientHandler([](SP<Hyprwire::IServerClient> client) { Core::clients->add(client); });
    m_loop.addFd(m_socket->extractLoopFD(), [this] { m_socket->dispatchEvents(false); });

    g_logger->log(LOG_DEBUG, "listening on {}", m_socketPath.string());

    if (!initBarmaids())
        return;

    m_good = true;
}

CTavern::~CTavern() {
    // dismisses pending prompts, which still needs the barmaids
    PROTO::core.reset();

    m_barmaidListeners.clear();
    Barmaids::kv.reset();
    Barmaids::permissions.reset();

    // tears down all clients, which still need the managers
    m_socket.reset();

    Core::clients.reset();
    Bus::tokens.reset();
    Bus::registry.reset();

    if (!m_socketPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_socketPath, ec);
    }

    // the lock file stays: unlinking it would let another instance lock a new inode while we still run
    for (const auto SIGNAL : HANDLED_SIGNALS) {
        std::signal(SIGNAL, SIG_DFL);
    }

    g_signalFd = -1;
}

bool CTavern::initRuntimeDir() {
    const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir || !*runtimeDir) {
        g_logger->log(LOG_ERR, "XDG_RUNTIME_DIR needs to be set");
        return false;
    }

    const auto      DIR = std::filesystem::path{runtimeDir} / "hyprtavern";

    std::error_code ec;
    std::filesystem::create_directories(DIR, ec);
    if (ec) {
        g_logger->log(LOG_ERR, "failed to create the runtime directory {}: {}", DIR.string(), ec.message());
        return false;
    }

    m_lockFd = CFileDescriptor{open((DIR / LOCK_FILE_NAME).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600)};
    if (!m_lockFd.isValid()) {
        g_logger->log(LOG_ERR, "failed to open the lock file: {}", strerror(errno));
        return false;
    }

    if (flock(m_lockFd.get(), LOCK_EX | LOCK_NB) < 0) {
        g_logger->log(LOG_ERR, "hyprtavern is already running for this user");
        return false;
    }

    // stale from a previous instance, we hold the lock
    m_socketPath = DIR / SOCKET_FILE_NAME;
    std::filesystem::remove(m_socketPath, ec);

    return true;
}

bool CTavern::initSignals() {
    int fds[2] = {-1, -1};
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0) {
        g_logger->log(LOG_ERR, "failed to create the signal pipe: {}", strerror(errno));
        return false;
    }

    m_signalReadFd  = CFileDescriptor{fds[0]};
    m_signalWriteFd = CFileDescriptor{fds[1]};
    g_signalFd      = m_signalWriteFd.get();

    struct sigaction action = {};
    action.sa_handler       = onSignal;
    sigemptyset(&action.sa_mask);

    for (const auto SIGNAL : HANDLED_SIGNALS) {
        sigaction(SIGNAL, &action, nullptr);
    }

    // a peer closing its socket must not kill the tavern
    std::signal(SIGPIPE, SIG_IGN);

    m_loop.addFd(m_signalReadFd.get(), [this] { onSignals(); });

    return true;
}

bool CTavern::initBarmaids() {
    Barmaids::kv          = makeUnique<Barmaids::CKvBarmaid>();
    Barmaids::permissions = makeUnique<Barmaids::CPermissionBarmaid>();

    for (Barmaids::CBarmaid* barmaid : {sc<Barmaids::CBarmaid*>(Barmaids::kv.get()), sc<Barmaids::CBarmaid*>(Barmaids::permissions.get())}) {
        m_barmaidListeners.emplace_back(barmaid->m_events.ready.listen([this] { onBarmaidReady(); }));

        // the tavern cannot work without its barmaids
        m_barmaidListeners.emplace_back(barmaid->m_events.died.listen([this] { exit(false); }));

        if (!barmaid->launch(m_socket, m_loop))
            return false;
    }

    m_loop.setDeadline(std::chrono::steady_clock::now() + STARTUP_TIMEOUT, [this] {
        g_logger->log(LOG_ERR, "barmaids did not become ready in time");
        exit(false);
    });

    return true;
}

void CTavern::onBarmaidReady() {
    if (!Barmaids::kv->ready() || !Barmaids::permissions->ready())
        return;

    g_logger->log(LOG_DEBUG, "all barmaids are ready, the tavern is open");

    m_loop.clearDeadline();
    PROTO::core->setReady();
}

void CTavern::onSignals() {
    uint8_t signal = 0;

    while (read(m_signalReadFd.get(), &signal, 1) == 1) {
        if (signal == SIGCHLD) {
            Barmaids::kv->reap();
            Barmaids::permissions->reap();
            continue;
        }

        g_logger->log(LOG_DEBUG, "received signal {}, exiting", signal);
        exit(true);
    }
}

bool CTavern::good() const {
    return m_good;
}

bool CTavern::run() {
    if (!m_loop.run()) {
        g_logger->log(LOG_ERR, "polling failed: {}", strerror(errno));
        return false;
    }

    return m_success;
}

void CTavern::exit(bool success) {
    m_success = m_success && success;
    m_loop.exit();
}

void CTavern::updateEnvironment(const std::vector<const char*>& names, const std::vector<const char*>& values) {
    g_logger->log(LOG_DEBUG, "updating {} environment variables", names.size());

    Environment::applyUpdate(names, values);

    Barmaids::kv->updateEnvironment(names, values);
    Barmaids::permissions->updateEnvironment(names, values);
}
