#include "BarmaidProcess.hpp"

#include "../helpers/Logger.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
    class COwnedFD {
      public:
        explicit COwnedFD(int fd = -1) : m_fd(fd) {}
        ~COwnedFD() {
            reset();
        }

        COwnedFD(const COwnedFD&)            = delete;
        COwnedFD& operator=(const COwnedFD&) = delete;

        COwnedFD(COwnedFD&& other) noexcept : m_fd(other.release()) {}
        COwnedFD& operator=(COwnedFD&& other) noexcept {
            if (this != &other)
                reset(other.release());
            return *this;
        }

        int get() const {
            return m_fd;
        }

        int release() {
            const int fd = m_fd;
            m_fd         = -1;
            return fd;
        }

        void reset(int fd = -1) {
            if (m_fd >= 0)
                close(m_fd);
            m_fd = fd;
        }

      private:
        int m_fd = -1;
    };

    bool setCloseOnExec(int fd) {
        const int FLAGS = fcntl(fd, F_GETFD);
        return FLAGS >= 0 && fcntl(fd, F_SETFD, FLAGS | FD_CLOEXEC) >= 0;
    }

    bool reapBlocking(pid_t pid) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno == EINTR)
                continue;
            return errno == ECHILD;
        }
        return true;
    }

    bool waitUntil(pid_t pid, std::chrono::steady_clock::time_point deadline) {
        while (true) {
            int       status = 0;
            const int ret    = waitpid(pid, &status, WNOHANG);
            if (ret == pid)
                return true;
            if (ret < 0) {
                if (errno == EINTR)
                    continue;
                return errno == ECHILD;
            }

            const auto NOW = std::chrono::steady_clock::now();
            if (NOW >= deadline)
                return false;

            std::this_thread::sleep_for(std::min(std::chrono::milliseconds(20), std::chrono::duration_cast<std::chrono::milliseconds>(deadline - NOW)));
        }
    }
}

pid_t CBarmaidProcess::launch(const std::string& app, const std::vector<std::string>& params) {
    std::vector<std::string> arguments;
    arguments.reserve(params.size() + 1);
    arguments.emplace_back(app);
    arguments.insert(arguments.end(), params.begin(), params.end());

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments)
        argv.emplace_back(argument.data());
    argv.emplace_back(nullptr);

    int execStatusFds[2] = {-1, -1};
    if (pipe(execStatusFds) < 0) {
        g_logger->log(LOG_ERR, "failed to create exec status pipe for {}: {}", app, std::strerror(errno));
        return -1;
    }

    COwnedFD execStatusRead{execStatusFds[0]};
    COwnedFD execStatusWrite{execStatusFds[1]};
    if (!setCloseOnExec(execStatusRead.get()) || !setCloseOnExec(execStatusWrite.get())) {
        g_logger->log(LOG_ERR, "failed to set exec status pipe close-on-exec for {}: {}", app, std::strerror(errno));
        return -1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        g_logger->log(LOG_ERR, "failed to fork for exec {}: {}", app, std::strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execStatusRead.reset();
        execvp(app.c_str(), argv.data());

        const int   EXEC_ERRNO = errno;
        const char* data       = reinterpret_cast<const char*>(&EXEC_ERRNO);
        size_t      written    = 0;
        while (written < sizeof(EXEC_ERRNO)) {
            const ssize_t ret = write(execStatusWrite.get(), data + written, sizeof(EXEC_ERRNO) - written);
            if (ret > 0) {
                written += static_cast<size_t>(ret);
                continue;
            }
            if (ret < 0 && errno == EINTR)
                continue;
            break;
        }
        _exit(127);
    }

    execStatusWrite.reset();

    constexpr auto EXEC_TIMEOUT = std::chrono::seconds(5);
    const auto     DEADLINE     = std::chrono::steady_clock::now() + EXEC_TIMEOUT;
    while (true) {
        const auto NOW = std::chrono::steady_clock::now();
        if (NOW >= DEADLINE) {
            g_logger->log(LOG_ERR, "timed out waiting for {} to exec", app);
            terminate(pid);
            errno = ETIMEDOUT;
            return -1;
        }

        const auto REMAINING = std::chrono::duration_cast<std::chrono::milliseconds>(DEADLINE - NOW);
        pollfd     pfd{.fd = execStatusRead.get(), .events = POLLIN, .revents = 0};
        const int  ret = poll(&pfd, 1, std::max(1, static_cast<int>(REMAINING.count())));
        if (ret > 0)
            break;
        if (ret == 0)
            continue;
        if (errno == EINTR)
            continue;

        const int POLL_ERRNO = errno;
        g_logger->log(LOG_ERR, "failed polling exec status for {}: {}", app, std::strerror(POLL_ERRNO));
        terminate(pid);
        errno = POLL_ERRNO;
        return -1;
    }

    int    execErrno = 0;
    size_t received  = 0;
    while (received < sizeof(execErrno)) {
        const ssize_t ret = read(execStatusRead.get(), reinterpret_cast<char*>(&execErrno) + received, sizeof(execErrno) - received);
        if (ret > 0) {
            received += static_cast<size_t>(ret);
            continue;
        }
        if (ret == 0)
            break;
        if (errno == EINTR)
            continue;

        const int READ_ERRNO = errno;
        g_logger->log(LOG_ERR, "failed reading exec status for {}: {}", app, std::strerror(READ_ERRNO));
        terminate(pid);
        errno = READ_ERRNO;
        return -1;
    }

    if (received == 0)
        return pid;

    reapBlocking(pid);
    if (received != sizeof(execErrno))
        execErrno = EIO;

    g_logger->log(LOG_ERR, "failed to exec {}: {}", app, std::strerror(execErrno));
    errno = execErrno;
    return -1;
}

bool CBarmaidProcess::isRunning(pid_t pid) {
    if (pid <= 0)
        return false;

    while (true) {
        int         status = 0;
        const pid_t ret    = waitpid(pid, &status, WNOHANG);
        if (ret == 0)
            return true;
        if (ret == pid)
            return false;
        if (errno == EINTR)
            continue;
        return false;
    }
}

void CBarmaidProcess::terminate(pid_t pid, std::chrono::milliseconds gracePeriod) {
    if (pid <= 0)
        return;

    int   status  = 0;
    pid_t initial = -1;
    do {
        initial = waitpid(pid, &status, WNOHANG);
    } while (initial < 0 && errno == EINTR);

    if (initial == pid || (initial < 0 && errno == ECHILD))
        return;
    if (initial < 0) {
        g_logger->log(LOG_WARN, "failed to inspect barmaid pid {}: {}", pid, std::strerror(errno));
        return;
    }

    if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
        g_logger->log(LOG_WARN, "failed to send SIGTERM to barmaid pid {}: {}", pid, std::strerror(errno));

    if (waitUntil(pid, std::chrono::steady_clock::now() + gracePeriod))
        return;

    g_logger->log(LOG_WARN, "barmaid pid {} did not exit after SIGTERM; sending SIGKILL", pid);
    if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
        g_logger->log(LOG_WARN, "failed to send SIGKILL to barmaid pid {}: {}", pid, std::strerror(errno));
        return;
    }

    if (!reapBlocking(pid))
        g_logger->log(LOG_WARN, "failed to reap barmaid pid {}: {}", pid, std::strerror(errno));
}
