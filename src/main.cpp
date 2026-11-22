#include "Tavern.hpp"

#include <common/Log.hpp>

#include <hyprutils/cli/ArgumentParser.hpp>
#include <hyprutils/misc/HyprAssert.hpp>

#include <print>

using namespace Hyprutils::CLI;

int main(int argc, const char** argv) {
    CArgumentParser parser({argv, sc<size_t>(argc)});

    HYPRUTILS_ASSERT(parser.registerBoolOption("verbose", "", "Enable more logging"));
    HYPRUTILS_ASSERT(parser.registerBoolOption("enforce-permissions", "", "Enforce permissions for unsandboxed host applications too"));
    HYPRUTILS_ASSERT(parser.registerBoolOption("help", "h", "Show the help menu"));

    if (const auto RET = parser.parse(); !RET) {
        g_logger->log(LOG_ERR, "Failed parsing arguments: {}", RET.error());
        return 1;
    }

    if (parser.getBool("help").value_or(false)) {
        std::println("{}", parser.getDescription(std::format("hyprtavern v{}", HYPRTAVERN_VERSION)));
        return 0;
    }

    if (parser.getBool("verbose").value_or(false))
        g_logger->setLogLevel(LOG_TRACE);

    g_tavern = makeUnique<CTavern>(STavernOptions{.enforcePermissions = parser.getBool("enforce-permissions").value_or(false)});

    const bool SUCCESS = g_tavern->good() && g_tavern->run();

    g_tavern.reset();

    return SUCCESS ? 0 : 1;
}
