#include <hyprwire/hyprwire.hpp>
#include <print>
#include <hp_hyprtavern_core_v1-client.hpp>
#include <hp_hyprtavern_kv_store_v1-client.hpp>

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

constexpr const uint32_t                        PROTOCOL_VERSION    = 1;
constexpr const uint32_t                        KV_PROTOCOL_VERSION = 1;
constexpr const char*                           KV_TOKEN_NAME       = "core:security_token";

static SP<CCHpHyprtavernCoreV1Impl>             impl   = makeShared<CCHpHyprtavernCoreV1Impl>(PROTOCOL_VERSION);
static SP<CCHpHyprtavernKvStoreV1Impl>          kvImpl = makeShared<CCHpHyprtavernKvStoreV1Impl>(KV_PROTOCOL_VERSION);
static SP<CCHpHyprtavernCoreManagerV1Object>    manager;
static SP<CCHpHyprtavernSecurityObjectV1Object> security;
static SP<CCHpHyprtavernBusQueryV1Object>       query;
static SP<Hyprwire::IClientSocket>              sock, kvSock;

static SP<CCHpHyprtavernKvStoreManagerV1Object> kvManager;

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

static bool createNewSecurityObject(const std::string& token = "") {
    security = makeShared<CCHpHyprtavernSecurityObjectV1Object>(manager->sendGetSecurityObject(token.c_str()));
    security->sendSetIdentity("hyprtavern-env", "Hyprtavern env utility");
    security->sendObtainPermission(HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MANAGEMENT_ENVIRONMENT, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT);

    security->setToken([](const char* tk) {
        if (!kvManager)
            return;

        kvManager->sendSetValue(KV_TOKEN_NAME, tk, HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE);
    });

    std::optional<bool> permissionDone;
    bool                unavailable = false;

    security->setPermissionResult([&permissionDone](uint32_t perm, uint32_t result) {
        permissionDone = result == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED_BY_POLICY || result == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED ||
            result == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_ALREADY_GRANTED;
    });

    security->setUnavailable([&unavailable] { unavailable = true; });

    while (!permissionDone.has_value() && !unavailable) {
        sock->dispatchEvents(true);
    }

    if (unavailable) {
        std::print("err: permissions unavailable, can't update env");
        return false;
    }

    if (!*permissionDone) {
        std::print("warning: permission to manage bus env denied, can't update env");
        return false;
    }

    return true;
}

static bool setupSecurityObject() {
    // first, try to find our token in kv
    auto kvQuery = makeShared<CCHpHyprtavernBusQueryV1Object>(manager->sendGetQueryObject({kvImpl->protocol()->specName().c_str()}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL,
                                                                                          {}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL));

    uint32_t objectId = 0;
    kvQuery->setResults([&objectId](const std::vector<uint32_t>& res) {
        if (res.empty())
            return;

        objectId = res[0];
    });

    sock->roundtrip();

    if (objectId == 0)
        return createNewSecurityObject();

    auto handle = makeShared<CCHpHyprtavernBusObjectHandleV1Object>(manager->sendGetObjectHandle(objectId));

    handle->sendConnect();

    int fd = -1;

    handle->setSocket([&fd](int f) { fd = f; });

    sock->roundtrip();

    if (fd <= 0)
        return createNewSecurityObject();

    kvSock = Hyprwire::IClientSocket::open(fd);

    kvSock->addImplementation(kvImpl);

    if (!kvSock->waitForHandshake())
        return createNewSecurityObject();

    kvManager = makeShared<CCHpHyprtavernKvStoreManagerV1Object>(kvSock->bindProtocol(kvImpl->protocol(), KV_PROTOCOL_VERSION));

    std::string valueObtained;

    kvManager->sendGetValue(KV_TOKEN_NAME, HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE);
    kvManager->setValueObtained([&valueObtained](const char* k, const char* v, uint32_t type) { valueObtained = v; });

    kvSock->roundtrip();

    if (valueObtained.empty())
        return createNewSecurityObject();

    // create security object from this token
    return createNewSecurityObject(valueObtained);
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

    if (!setupSecurityObject())
        return 1;

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

    return 0;
}