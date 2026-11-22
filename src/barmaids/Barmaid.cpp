#include "Barmaid.hpp"
#include "../core/ClientManager.hpp"

#include <common/Log.hpp>
#include <common/Wire.hpp>

#include <hyprutils/os/FileDescriptor.hpp>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace Hyprutils::OS;
using namespace Barmaids;

constexpr uint32_t BARMAID_PROTOCOL_VERSION = 1;
constexpr auto     TERMINATION_GRACE_PERIOD = std::chrono::seconds{2};

namespace {
    // the inherited fds stay open across exec
    pid_t spawn(const std::vector<std::string>& args, const std::vector<int>& inherited) {
        std::vector<char*> argv;

        for (const auto& arg : args) {
            argv.emplace_back(cc<char*>(arg.c_str()));
        }

        argv.emplace_back(nullptr);

        const pid_t PID = fork();
        if (PID != 0)
            return PID;

        // only async-signal-safe calls until exec
        for (const auto FD : inherited) {
            fcntl(FD, F_SETFD, 0);
        }

        signal(SIGPIPE, SIG_DFL);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    bool socketPair(CFileDescriptor& ours, CFileDescriptor& theirs) {
        int fds[2] = {-1, -1};
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0)
            return false;

        ours   = CFileDescriptor{fds[0]};
        theirs = CFileDescriptor{fds[1]};
        return true;
    }
}

CBarmaid::CBarmaid(std::string binary, Bus::eProviderRole role, bool needsBus) : m_binary(std::move(binary)), m_role(role), m_needsBus(needsBus) {
    ;
}

CBarmaid::~CBarmaid() {
    terminate();
}

bool CBarmaid::launch(const SP<Hyprwire::IServerSocket>& bus, CEventLoop& loop) {
    CFileDescriptor control, childControl, busEnd, childBus;

    if (!socketPair(control, childControl) || (m_needsBus && !socketPair(busEnd, childBus))) {
        g_logger->log(LOG_ERR, "failed to create sockets for {}: {}", m_binary, strerror(errno));
        return false;
    }

    std::vector<std::string> args = {m_binary, "--control-fd", std::to_string(childControl.get())};
    if (m_needsBus) {
        args.emplace_back("--tavern-fd");
        args.emplace_back(std::to_string(childBus.get()));
    }

    m_pid = spawn(args, {childControl.get(), childBus.get()});
    if (m_pid < 0) {
        g_logger->log(LOG_ERR, "failed to start {}: {}", m_binary, strerror(errno));
        return false;
    }

    g_logger->log(LOG_DEBUG, "started {} with pid {}", m_binary, m_pid);

    // the bus connection is identified by the tavern, never by its credentials
    if (m_needsBus) {
        const auto WIRE = bus->addClient(busEnd.take());
        if (!WIRE)
            return false;

        Core::clients->add(WIRE, m_role, Security::SAppIdentity{.kind = Security::eIdentityKind::INTERNAL, .id = m_binary, .stable = true});
    }

    m_control = Hyprwire::IClientSocket::open(control.take());
    m_control->addImplementation(makeShared<CCHpHyprtavernBarmaidV1Impl>(BARMAID_PROTOCOL_VERSION));

    m_loop      = &loop;
    m_controlFd = m_control->extractLoopFD();
    m_loop->addFd(m_controlFd, [this] { dispatch(); });

    return true;
}

void CBarmaid::dispatch() {
    if (!m_control->dispatchEvents(false)) {
        g_logger->log(LOG_ERR, "lost the control channel of {}", m_binary);
        die();
        return;
    }

    if (m_connected || !m_control->isHandshakeDone())
        return;

    m_connected = true;

    const auto SPEC = makeShared<CCHpHyprtavernBarmaidV1Impl>(BARMAID_PROTOCOL_VERSION)->protocol();

    m_control->bindProtocolAsync(SPEC, BARMAID_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> object) {
        if (!object) {
            g_logger->log(LOG_ERR, "{} does not provide the barmaid protocol", m_binary);
            die();
            return;
        }

        m_manager = makeShared<CCHpHyprtavernBarmaidManagerV1Object>(std::move(object));
        m_manager->setReady([this] { onReady(); });

        for (const auto& update : m_pendingEnvironment) {
            m_manager->sendUpdateTavernEnvironment(Wire::toCStrings(update.names), Wire::toCStrings(update.values));
        }

        m_pendingEnvironment.clear();
    });

    onConnected();
}

void CBarmaid::onReady() {
    if (!m_ready) {
        g_logger->log(LOG_DEBUG, "{} is ready", m_binary);
        m_ready = true;
        m_events.ready.emit();
        return;
    }

    // once ready, ready events only answer status requests
    if (m_syncs.empty())
        return;

    auto done = std::move(m_syncs.front());
    m_syncs.pop_front();
    done();
}

void CBarmaid::sync(std::function<void()>&& done) {
    m_syncs.emplace_back(std::move(done));
    m_manager->sendRequestStatus();
}

bool CBarmaid::ready() const {
    return m_ready && !m_dead;
}

void CBarmaid::reap() {
    int status = 0;
    if (m_pid <= 0 || waitpid(m_pid, &status, WNOHANG) != m_pid)
        return;

    g_logger->log(LOG_ERR, "{} exited with status {}", m_binary, WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));

    m_pid = -1;
    die();
}

void CBarmaid::updateEnvironment(const std::vector<const char*>& names, const std::vector<const char*>& values) {
    if (!m_manager) {
        m_pendingEnvironment.emplace_back(SEnvironmentUpdate{.names = Wire::toStrings(names), .values = Wire::toStrings(values)});
        return;
    }

    m_manager->sendUpdateTavernEnvironment(names, values);
}

void CBarmaid::die() {
    if (m_dead)
        return;

    m_dead = true;

    if (m_loop)
        m_loop->removeFd(m_controlFd);

    m_events.died.emit();
}

void CBarmaid::terminate() {
    // well-behaved barmaids exit once their control channel closes
    m_manager.reset();
    m_control.reset();

    if (m_pid <= 0)
        return;

    kill(m_pid, SIGTERM);

    const auto DEADLINE = std::chrono::steady_clock::now() + TERMINATION_GRACE_PERIOD;

    while (waitpid(m_pid, nullptr, WNOHANG) == 0) {
        if (std::chrono::steady_clock::now() < DEADLINE) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            continue;
        }

        g_logger->log(LOG_WARN, "{} did not exit in time, killing it", m_binary);
        kill(m_pid, SIGKILL);
        waitpid(m_pid, nullptr, 0);
        break;
    }

    m_pid = -1;
}
