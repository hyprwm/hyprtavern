#include "BarmaidProcess.hpp"

#include "../helpers/Logger.hpp"
#include "../helpers/Memory.hpp"

#include <cerrno>

#include <sys/signal.h>
#include <unistd.h>

pid_t CBarmaidProcess::launch(const std::string& app, const std::vector<std::string>& params) {
    std::vector<const char*> argv = {app.c_str()};
    argv.reserve(params.size() + 2);

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
        _exit(1);
    }

    return fk;
}

bool CBarmaidProcess::isRunning(pid_t pid) {
    if (pid <= 0)
        return false;

    if (::kill(pid, 0) == 0)
        return true;

    if (errno == EPERM)
        return true;

    return false;
}

void CBarmaidProcess::terminate(pid_t pid) {
    if (!isRunning(pid))
        return;

    if (::kill(pid, SIGTERM) < 0)
        g_logger->log(LOG_WARN, "failed to terminate barmaid pid {}", pid);
}
