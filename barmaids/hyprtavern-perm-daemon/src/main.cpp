#include "core/Core.hpp"

#include <common/Log.hpp>

#include <hyprutils/cli/ArgumentParser.hpp>
#include <hyprutils/misc/HyprAssert.hpp>

#include <csignal>
#include <print>

using namespace Hyprutils::CLI;

int main(int argc, const char** argv) {
    CArgumentParser parser({argv, sc<size_t>(argc)});

    HYPRUTILS_ASSERT(parser.registerIntOption("control-fd", "", "The private control channel to the tavern"));
    HYPRUTILS_ASSERT(parser.registerBoolOption("verbose", "", "Enable more logging"));
    HYPRUTILS_ASSERT(parser.registerBoolOption("help", "h", "Show the help menu"));

    if (const auto RET = parser.parse(); !RET) {
        g_logger->log(LOG_ERR, "Failed parsing arguments: {}", RET.error());
        return 1;
    }

    if (parser.getBool("help").value_or(false)) {
        std::println("{}", parser.getDescription(std::format("hyprtavern-perm-daemon built as part of hyprtavern v{}", HYPRTAVERN_VERSION)));
        return 0;
    }

    if (parser.getBool("verbose").value_or(false))
        g_logger->setLogLevel(LOG_TRACE);

    const auto CONTROL_FD = parser.getInt("control-fd");
    if (!CONTROL_FD) {
        g_logger->log(LOG_ERR, "hyprtavern-perm-daemon is started by hyprtavern, it needs --control-fd");
        return 1;
    }

    // a peer closing its socket must not kill the daemon
    std::signal(SIGPIPE, SIG_IGN);

    g_core = makeUnique<CCore>();

    const bool SUCCESS = g_core->init(*CONTROL_FD) && g_core->run();

    g_core.reset();

    return SUCCESS ? 0 : 1;
}
