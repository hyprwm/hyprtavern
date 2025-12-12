#include <hyprwire/hyprwire.hpp>

#include "core/Core.hpp"
#include "helpers/Logger.hpp"

#include <print>

#include <hyprutils/cli/ArgumentParser.hpp>

using namespace Hyprutils::CLI;

#define ASSERT(expr)                                                                                                                                                               \
    if (!(expr)) {                                                                                                                                                                 \
        g_logger->log(LOG_CRIT, "Failed assertion at line {} in {}: {} was false", __LINE__,                                                                                       \
                      ([]() constexpr -> std::string { return std::string(__FILE__).substr(std::string(__FILE__).find("/src/") + 1); })(), #expr);                                 \
        std::abort();                                                                                                                                                              \
    }

int main(int argc, const char** argv, const char** envp) {
    CArgumentParser parser({argv, sc<size_t>(argc)});

    ASSERT(parser.registerIntOption("fd", "", "Pass a file descriptor for the wire connection."));
    ASSERT(parser.registerBoolOption("verbose", "", "Enable more logging"));
    ASSERT(parser.registerBoolOption("help", "h", "Show the help menu"));

    if (const auto ret = parser.parse(); !ret) {
        g_logger->log(LOG_ERR, "Failed parsing arguments: {}", ret.error());
        return 1;
    }

    if (parser.getBool("help").value_or(false)) {
        std::println("{}", parser.getDescription(std::format("hyprtavern-kv built as part of hyprtavern v{}", HYPRTAVERN_VERSION)));
        return 0;
    }

    if (parser.getBool("verbose").value_or(false))
        g_logger->setLogLevel(LOG_TRACE);

    const auto FD_ARG = parser.getInt("fd");

    if (!FD_ARG) {
        g_logger->log(LOG_ERR, "cannot run without --fd");
        return 1;
    }

    g_core = makeUnique<CCore>();
    if (!g_core->init(*FD_ARG)){
        g_logger->log(LOG_ERR, "failed starting kv");
        return 1;
    }

    g_core->run();
    
    return 0;
}