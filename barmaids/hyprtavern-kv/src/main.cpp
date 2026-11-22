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
    HYPRUTILS_ASSERT(parser.registerIntOption("tavern-fd", "", "The connection to the tavern's bus"));
    HYPRUTILS_ASSERT(parser.registerBoolOption("verbose", "", "Enable more logging"));
    HYPRUTILS_ASSERT(parser.registerBoolOption("help", "h", "Show the help menu"));

    if (const auto RET = parser.parse(); !RET) {
        g_logger->log(LOG_ERR, "Failed parsing arguments: {}", RET.error());
        return 1;
    }

    if (parser.getBool("help").value_or(false)) {
        std::println("{}", parser.getDescription(std::format("hyprtavern-kv built as part of hyprtavern v{}", HYPRTAVERN_VERSION)));
        return 0;
    }

    if (parser.getBool("verbose").value_or(false))
        g_logger->setLogLevel(LOG_TRACE);

    const auto CONTROL_FD = parser.getInt("control-fd");
    const auto TAVERN_FD  = parser.getInt("tavern-fd");

    if (!CONTROL_FD || !TAVERN_FD) {
        g_logger->log(LOG_ERR, "hyprtavern-kv is started by hyprtavern, it needs --control-fd and --tavern-fd");
        return 1;
    }

    const auto PATH = Kv::defaultPath();
    if (!PATH) {
        g_logger->log(LOG_ERR, "no location for the store: {}", PATH.error());
        return 1;
    }

    // never start over an unreadable store, that would lose its data
    auto store = Kv::CStore::open(*PATH);
    if (!store) {
        g_logger->log(LOG_ERR, "failed to open the store: {}", store.error());
        return 1;
    }

    // a peer closing its socket must not kill the store
    std::signal(SIGPIPE, SIG_IGN);

    g_core = makeUnique<CCore>(std::move(*store));

    const bool SUCCESS = g_core->init(*CONTROL_FD, *TAVERN_FD) && g_core->run();

    g_core.reset();

    return SUCCESS ? 0 : 1;
}
