#include <hyprwire/hyprwire.hpp>
#include <print>
#include <algorithm>
#include <hp_hyprtavern_core_v1-client.hpp>

#include <chrono>
#include <optional>
#include <thread>

#include <hyprutils/cli/ArgumentParser.hpp>
#include <hyprutils/string/VarList2.hpp>

using namespace Hyprutils::Memory;
using namespace Hyprutils::CLI;
using namespace Hyprutils::String;

#define SP CSharedPointer

#define ASSERT(expr)                                                                                                                                                               \
    if (!(expr)) {                                                                                                                                                                 \
        std::println("Failed assertion at line {} in {}: {} was false", __LINE__,                                                                                                  \
                     ([]() constexpr -> std::string { return std::string(__FILE__).substr(std::string(__FILE__).find("/src/") + 1); })(), #expr);                                  \
        std::abort();                                                                                                                                                              \
    }

constexpr const uint32_t                     PROTOCOL_VERSION = 1;
constexpr const auto                         READY_TIMEOUT    = std::chrono::seconds(10);

static SP<CCHpHyprtavernCoreV1Impl>          impl = makeShared<CCHpHyprtavernCoreV1Impl>(PROTOCOL_VERSION);
static SP<CCHpHyprtavernCoreManagerV1Object> manager;
static SP<Hyprwire::IClientSocket>           sock;

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

static bool waitForReady() {
    bool ready = false;
    manager->setReady([&ready] { ready = true; });

    const auto started = std::chrono::steady_clock::now();
    while (!ready && std::chrono::steady_clock::now() - started < READY_TIMEOUT) {
        if (!sock->dispatchEvents(false))
            return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!ready) {
        std::println("err: timed out waiting for tavern ready event");
        return false;
    }

    manager->setReady([] {});
    return true;
}

int main(int argc, const char** argv, char** envp) {
    const auto XDG_RUNTIME_DIR = getenv("XDG_RUNTIME_DIR");

    if (!XDG_RUNTIME_DIR) {
        std::println("err: no runtime dir");
        return 1;
    }

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

    sock = Hyprwire::IClientSocket::open(XDG_RUNTIME_DIR + std::string{"/hyprtavern/ht.sock"});

    if (!sock) {
        std::println("err: tavern is not serving beer");
        return 1;
    }

    sock->addImplementation(impl);

    if (!sock->waitForHandshake()) {
        std::println("err: handshake failed");
        return 1;
    }

    const auto SPEC = sock->getSpec(impl->protocol()->specName());

    if (!SPEC) {
        std::println("err: protocol unsupported");
        return 1;
    }

    manager = makeShared<CCHpHyprtavernCoreManagerV1Object>(sock->bindProtocol(impl->protocol(), PROTOCOL_VERSION));

    if (!waitForReady()) {
        std::println("err: failed waiting for tavern readiness");
        return 1;
    }

    // only verified, unsandboxed host apps may change the environment, no permission can be requested for it
    std::optional<uint32_t> result;
    manager->setUpdateTavernEnvironmentResult([&result](uint32_t r) { result = r; });

    {
        std::vector<const char*> names, values;
        names.reserve(state.envNames.size());
        values.reserve(state.envNames.size());

        if (state.mode == UPDATE_MODE_UPDATE) {
            for (size_t i = 0; i < state.envNames.size(); ++i) {
                auto* env          = getenv(state.envNames[i].c_str());
                state.envValues[i] = env ? env : "";
            }
        }

        for (size_t i = 0; i < state.envNames.size(); ++i) {
            names.emplace_back(state.envNames[i].c_str());
            values.emplace_back(state.envValues[i].c_str());
        }

        manager->sendUpdateTavernEnvironment(names, values);
    }

    sock->roundtrip();

    if (result != HP_HYPRTAVERN_CORE_V1_ENVIRONMENT_UPDATE_RESULT_APPLIED) {
        std::println("err: the tavern denied the update, only unsandboxed apps may change its environment");
        return 1;
    }

    return 0;
}
