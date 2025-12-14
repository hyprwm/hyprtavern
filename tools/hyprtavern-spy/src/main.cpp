#include <hyprwire/hyprwire.hpp>
#include <print>
#include <hp_hyprtavern_core_v1-client.hpp>
#include <hp_hyprtavern_kv_store_v1-client.hpp>

using namespace Hyprutils::Memory;

#define SP CSharedPointer

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

//

static bool createNewSecurityObject(const std::string& token = "") {
    security = makeShared<CCHpHyprtavernSecurityObjectV1Object>(manager->sendGetSecurityObject(token.c_str()));
    security->sendSetIdentity("hyprtavern-spy", "Hyprtavern spy utility");
    security->sendObtainPermission(HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING_ALL_BUS_OBJECTS, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT);

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

    if (unavailable)
        std::print("warning: permissions unavailable, results may be incomplete");
    else if (!*permissionDone)
        std::print("warning: permission to monitor all objects was denied, results may be incomplete");

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

int main(int argc, char** argv, char** envp) {
    const auto XDG_RUNTIME_DIR = getenv("XDG_RUNTIME_DIR");

    if (!XDG_RUNTIME_DIR) {
        std::println("err: no runtime dir");
        return 1;
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

    setupSecurityObject();

    {
        query = makeShared<CCHpHyprtavernBusQueryV1Object>(
            manager->sendGetQueryObject({}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL, {}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL));
    }

    query->setResults([](const std::vector<uint32_t>& ids) {
        if (ids.size() == 1)
            std::println("There is {} object in the tavern:", ids.size());
        else
            std::println("There are {} objects in the tavern:", ids.size());

        for (const auto& id : ids) {
            auto                     handle = makeShared<CCHpHyprtavernBusObjectHandleV1Object>(manager->sendGetObjectHandle(id));

            std::string              name;
            std::vector<std::string> protocols;
            std::vector<uint32_t>    revs;
            std::vector<std::string> props;

            handle->setName([&name](const char* str) { name = str; });
            handle->setProperties([&props](const std::vector<const char*>& p) {
                for (const auto& pp : p) {
                    props.emplace_back(pp);
                }
            });
            handle->setProtocols([&protocols, &revs](const std::vector<const char*>& pn, const std::vector<uint32_t>& pr) {
                for (const auto& x : pn) {
                    protocols.emplace_back(x);
                }

                for (const auto& x : pr) {
                    revs.emplace_back(x);
                }
            });

            sock->roundtrip();

            std::println(" ┣╸{}#{}:", name, id);
            std::println(" ┃   ┣╸protocols:", name, id);
            for (size_t i = 0; i < protocols.size(); ++i) {
                std::println(" ┃   ┃   {}╸{}@{}", i == protocols.size() - 1 ? "┗" : "┣", protocols.at(i), revs.at(i));
            }
            std::println(" ┃   ┗╸props:");
            for (size_t i = 0; i < props.size(); ++i) {
                std::println(" ┃       {}╸{}", i == props.size() - 1 ? "┗" : "┣", props.at(i));
            }
        }

        sock->roundtrip();
    });

    sock->roundtrip();

    return 0;
}