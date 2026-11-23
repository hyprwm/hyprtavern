#include <bus/Registry.hpp>

#include <common/PermissionName.hpp>

#include <gtest/gtest.h>

#include <format>

using namespace Bus;

namespace {
    constexpr auto ANYONE = [](const SObject&) { return true; };

    uint32_t       addObject(CRegistry& registry, const std::string& name = "object") {
        return *registry.add(1, {}, name, {});
    }

    SProtocol protocol(const std::string& name, bool exclusive = false, std::vector<std::string> requirements = {}) {
        return {.name = name, .version = 1, .requirements = std::move(requirements), .exclusive = exclusive};
    }
}

TEST(Registry, NeverReusesIDs) {
    CRegistry  registry;

    const auto FIRST = addObject(registry);
    registry.remove(FIRST);
    const auto SECOND = addObject(registry);

    EXPECT_NE(FIRST, SECOND);
    EXPECT_EQ(registry.get(FIRST), nullptr);
    EXPECT_NE(registry.get(SECOND), nullptr);
}

TEST(Registry, BoundsLiveObjects) {
    CRegistry registry;

    // spread over owners, so only the bus-wide bound applies
    for (size_t i = 0; i < MAX_OBJECTS; ++i) {
        ASSERT_TRUE(registry.add(1 + i / MAX_OBJECTS_PER_OWNER, {}, "object", {}));
    }

    EXPECT_EQ(registry.add(0, {}, "object", {}).error(), eRegistryError::LIMIT_REACHED);

    registry.remove(1);
    EXPECT_TRUE(registry.add(0, {}, "object", {}));
}

TEST(Registry, BoundsObjectsPerOwner) {
    CRegistry registry;

    for (size_t i = 0; i < MAX_OBJECTS_PER_OWNER; ++i) {
        ASSERT_TRUE(registry.add(1, {}, "object", {}));
    }

    EXPECT_EQ(registry.add(1, {}, "object", {}).error(), eRegistryError::LIMIT_REACHED);
    EXPECT_TRUE(registry.add(2, {}, "object", {}));

    registry.remove(1);
    EXPECT_TRUE(registry.add(1, {}, "object", {}));
}

TEST(Registry, ValidatesRegistrations) {
    CRegistry  registry;
    const auto ID = addObject(registry);

    EXPECT_EQ(registry.exposeProtocol(ID, eProviderRole::NONE, protocol("")).error(), eRegistryError::INVALID_REGISTRATION);
    EXPECT_EQ(registry.exposeProtocol(ID, eProviderRole::NONE, {.name = "example_v1", .version = 0}).error(), eRegistryError::INVALID_REGISTRATION);
    EXPECT_TRUE(registry.exposeProtocol(ID, eProviderRole::NONE, protocol("example_v1")));
}

TEST(Registry, EnforcesExclusivityBothWays) {
    CRegistry  registry;
    const auto A = addObject(registry), B = addObject(registry), C = addObject(registry);

    ASSERT_TRUE(registry.exposeProtocol(A, eProviderRole::NONE, protocol("shared_v1")));
    EXPECT_TRUE(registry.exposeProtocol(B, eProviderRole::NONE, protocol("shared_v1")));
    EXPECT_EQ(registry.exposeProtocol(C, eProviderRole::NONE, protocol("shared_v1", true)).error(), eRegistryError::ALREADY_EXPOSED);

    ASSERT_TRUE(registry.exposeProtocol(A, eProviderRole::NONE, protocol("exclusive_v1", true)));
    EXPECT_EQ(registry.exposeProtocol(B, eProviderRole::NONE, protocol("exclusive_v1")).error(), eRegistryError::ALREADY_EXPOSED);

    registry.remove(A);
    EXPECT_TRUE(registry.exposeProtocol(B, eProviderRole::NONE, protocol("exclusive_v1", true)));
}

TEST(Registry, ReplacesRegistrationsOnTheSameObject) {
    CRegistry  registry;
    const auto A = addObject(registry), B = addObject(registry);

    ASSERT_TRUE(registry.exposeProtocol(A, eProviderRole::NONE, protocol("example_v1")));
    ASSERT_TRUE(registry.exposeProtocol(A, eProviderRole::NONE, {.name = "example_v1", .version = 3, .exclusive = true}));

    ASSERT_EQ(registry.get(A)->protocols.size(), 1);
    EXPECT_EQ(registry.get(A)->protocols.at("example_v1").version, 3);

    // a failed replacement keeps the previous registration
    ASSERT_TRUE(registry.exposeProtocol(B, eProviderRole::NONE, protocol("shared_v1")));
    ASSERT_TRUE(registry.exposeProtocol(A, eProviderRole::NONE, protocol("shared_v1")));
    EXPECT_FALSE(registry.exposeProtocol(A, eProviderRole::NONE, protocol("shared_v1", true)));
    EXPECT_FALSE(registry.get(A)->protocol("shared_v1")->exclusive);
}

TEST(Registry, ReservesInfrastructureProtocols) {
    CRegistry  registry;
    const auto ID = addObject(registry);

    EXPECT_EQ(registry.exposeProtocol(ID, eProviderRole::KV_STORE, protocol(std::string{PROTOCOL_BARMAID}, true)).error(), eRegistryError::UNAUTHORIZED_PROVIDER);
    EXPECT_EQ(registry.exposeProtocol(ID, eProviderRole::NONE, protocol(std::string{PROTOCOL_KV_STORE}, true)).error(), eRegistryError::UNAUTHORIZED_PROVIDER);
    EXPECT_EQ(registry.exposeProtocol(ID, eProviderRole::KV_STORE, protocol(std::string{PROTOCOL_PERMISSION_AUTHENTICATION}, true)).error(), eRegistryError::UNAUTHORIZED_PROVIDER);

    EXPECT_EQ(registry.exposeProtocol(ID, eProviderRole::KV_STORE, protocol(std::string{PROTOCOL_KV_STORE})).error(), eRegistryError::INVALID_REGISTRATION);
    EXPECT_TRUE(registry.exposeProtocol(ID, eProviderRole::KV_STORE, protocol(std::string{PROTOCOL_KV_STORE}, true)));
}

TEST(Registry, RequiresTavernkeepForPermissionAuthentication) {
    CRegistry  registry;
    const auto ID   = addObject(registry);
    const auto ROLE = eProviderRole::PERMISSION_AUTHENTICATION;
    const auto NAME = std::string{PROTOCOL_PERMISSION_AUTHENTICATION};

    EXPECT_EQ(registry.exposeProtocol(ID, ROLE, protocol(NAME, true)).error(), eRegistryError::INVALID_REGISTRATION);
    EXPECT_EQ(registry.exposeProtocol(ID, ROLE, protocol(NAME, false, {std::string{Security::PERMISSION_TAVERNKEEP}})).error(), eRegistryError::INVALID_REGISTRATION);
    EXPECT_TRUE(registry.exposeProtocol(ID, ROLE, protocol(NAME, true, {std::string{Security::PERMISSION_TAVERNKEEP}})));
}

TEST(Registry, BoundsProtocolsAndProperties) {
    CRegistry  registry;
    const auto ID = addObject(registry);

    for (size_t i = 0; i < MAX_PROTOCOLS; ++i) {
        ASSERT_TRUE(registry.exposeProtocol(ID, eProviderRole::NONE, protocol(std::format("p{}", i))));
        ASSERT_TRUE(registry.exposeProperty(ID, std::format("GLOBAL:p{}", i), "x"));
    }

    EXPECT_EQ(registry.exposeProtocol(ID, eProviderRole::NONE, protocol("overflow")).error(), eRegistryError::LIMIT_REACHED);
    EXPECT_EQ(registry.exposeProperty(ID, "GLOBAL:overflow", "x").error(), eRegistryError::LIMIT_REACHED);

    // replacing or removing does not take a slot
    EXPECT_TRUE(registry.exposeProtocol(ID, eProviderRole::NONE, protocol("p0", true)));
    EXPECT_TRUE(registry.exposeProperty(ID, "GLOBAL:p0", "y"));
    EXPECT_TRUE(registry.exposeProperty(ID, "GLOBAL:p0", ""));
    EXPECT_TRUE(registry.exposeProperty(ID, "GLOBAL:overflow", "x"));
}

TEST(Registry, ValidatesPropertyNames) {
    EXPECT_TRUE(isValidPropertyName("GLOBAL:WAYLAND_DISPLAY"));
    EXPECT_TRUE(isValidPropertyName("my_proto+1:value"));
    EXPECT_FALSE(isValidPropertyName("GLOBAL"));
    EXPECT_FALSE(isValidPropertyName(":name"));
    EXPECT_FALSE(isValidPropertyName("group:"));
    EXPECT_FALSE(isValidPropertyName("a:b:c"));
    EXPECT_FALSE(isValidPropertyName("group:na-me"));

    EXPECT_EQ(parseQueryProperty("GLOBAL:A=b=c"), (std::pair<std::string, std::string>{"GLOBAL:A", "b=c"}));
    EXPECT_EQ(parseQueryProperty("GLOBAL:A"), std::nullopt);
    EXPECT_EQ(parseQueryProperty("bad name=x"), std::nullopt);
}

TEST(Registry, QueriesByProtocolsAndProperties) {
    CRegistry  registry;
    const auto A = addObject(registry), B = addObject(registry), C = addObject(registry);

    registry.exposeProtocol(A, eProviderRole::NONE, protocol("one_v1"));
    registry.exposeProtocol(A, eProviderRole::NONE, protocol("two_v1"));
    registry.exposeProtocol(B, eProviderRole::NONE, protocol("two_v1"));
    registry.exposeProperty(A, "GLOBAL:DISPLAY", ":1");
    registry.exposeProperty(B, "GLOBAL:DISPLAY", ":2");

    using IDs = std::vector<uint32_t>;

    EXPECT_EQ(registry.query({}, ANYONE), (IDs{A, B, C}));
    EXPECT_EQ(registry.query({.protocols = {"one_v1", "two_v1"}}, ANYONE), (IDs{A}));
    EXPECT_EQ(registry.query({.protocols = {"one_v1", "two_v1"}, .protocolMode = eFilterMode::ANY}, ANYONE), (IDs{A, B}));
    EXPECT_EQ(registry.query({.properties = {{"GLOBAL:DISPLAY", ":2"}}}, ANYONE), (IDs{B}));
    EXPECT_EQ(registry.query({.properties = {{"GLOBAL:DISPLAY", ":1"}, {"GLOBAL:DISPLAY", ":2"}}, .propertyMode = eFilterMode::ANY}, ANYONE), (IDs{A, B}));
    EXPECT_EQ(registry.query({.protocols = {"two_v1"}, .properties = {{"GLOBAL:DISPLAY", ":1"}}}, ANYONE), (IDs{A}));
    EXPECT_EQ(registry.query({.protocols = {"missing_v1"}, .protocolMode = eFilterMode::ANY}, ANYONE), IDs{});
}

TEST(Registry, QueriesOnlyVisibleObjects) {
    CRegistry  registry;
    const auto A = addObject(registry, "public"), B = addObject(registry, "hidden");

    EXPECT_EQ(registry.query({}, [](const SObject& object) { return object.name == "public"; }), std::vector<uint32_t>{A});
    EXPECT_NE(A, B);
}
