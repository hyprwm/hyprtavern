#include <hyprtavern-client/hyprtavern-client.hpp>

#include <print>

#include <hyprutils/cli/ArgumentParser.hpp>
#include <hyprutils/memory/Casts.hpp>
#include <hyprutils/string/VarList2.hpp>

using namespace Hyprutils::Memory;
using namespace Hyprutils::CLI;
using namespace Hyprutils::String;
using namespace Hyprtavern;

#define ASSERT(expr)                                                                                                                                                               \
    if (!(expr)) {                                                                                                                                                                 \
        std::println("Failed assertion at line {} in {}: {} was false", __LINE__,                                                                                                  \
                     ([]() constexpr -> std::string { return std::string(__FILE__).substr(std::string(__FILE__).find("/src/") + 1); })(), #expr);                                  \
        std::abort();                                                                                                                                                              \
    }

enum eUpdateMode : uint8_t {
    UPDATE_MODE_UPDATE,
    UPDATE_MODE_SET,
};

struct SState {
    eUpdateMode              mode = UPDATE_MODE_UPDATE;
    std::vector<std::string> envNames, envValues;
};

static SState state;

//

int main(int argc, const char** argv, char** envp) {
    CArgumentParser parser({argv, sc<size_t>(argc)});

    ASSERT(parser.registerBoolOption("help", "h", "Show this menu"));
    ASSERT(parser.registerBoolOption("set", "", "If passed, pass values as NAME=VALUE"));
    ASSERT(parser.registerBoolOption("update", "", "If passed, pass values as NAME, and values will be taken from the env of the executing env"));
    ASSERT(parser.registerStringOption("env", "", "Space-separated environment variable list"));

    if (const auto ret = parser.parse(); !ret) {
        std::println("failed parsing arguments: {}", ret.error());
        return 1;
    }

    if (parser.getBool("help").value_or(false)) {
        std::println("{}", parser.getDescription(std::format("hyprtavern-env built as part of hyprtavern v{}", HYPRTAVERN_VERSION)));
        return 0;
    }

    if (parser.getBool("set"))
        state.mode = UPDATE_MODE_SET;
    else if (parser.getBool("update"))
        state.mode = UPDATE_MODE_UPDATE;
    else {
        std::println("missing mode --set / --update");
        return 1;
    }

    if (state.mode == UPDATE_MODE_UPDATE) {
        auto ENV = std::string{parser.getString("env").value_or("")};

        if (ENV.empty()) {
            std::println("missing --env");
            return 1;
        }

        CVarList2 varlist(std::move(ENV), 0, 's');
        for (const auto& v : varlist) {
            state.envNames.emplace_back(v);
        }
        state.envValues.resize(state.envNames.size());
    } else {
        auto ENV = std::string{parser.getString("env").value_or("")};

        if (ENV.empty()) {
            std::println("missing --env");
            return 1;
        }

        CVarList2 varlist(std::move(ENV), 0, 's');
        for (const auto& v : varlist) {
            size_t eqPos = v.find('=');
            if (eqPos == std::string::npos) {
                std::println("invalid env: {}", v);
                return 1;
            }
            state.envNames.emplace_back(v.substr(0, eqPos));
            state.envValues.emplace_back(v.substr(eqPos + 1));
        }
    }

    const auto TAVERN = ITavern::open({.appID = "org.hyprland.hyprtavern.env"});
    if (!TAVERN) {
        std::println("err: {}", errorString(TAVERN.error()));
        return 1;
    }

    EnvironmentUpdate update;

    for (size_t i = 0; i < state.envNames.size(); ++i) {
        if (state.mode == UPDATE_MODE_UPDATE) {
            auto* env          = getenv(state.envNames[i].c_str());
            state.envValues[i] = env ? env : "";
        }

        update.emplace_back(state.envNames[i], state.envValues[i]);
    }

    // only verified, unsandboxed host apps may change the environment, no permission can be requested for it
    if (const auto RET = (*TAVERN)->updateEnvironmentSync(update); !RET) {
        if (RET.error() == eError::DENIED)
            std::println("err: the tavern denied the update, only unsandboxed apps may change its environment");
        else
            std::println("err: failed to update the environment: {}", errorString(RET.error()));

        return 1;
    }

    return 0;
}
