#include <core/Client.hpp>

#include <common/PermissionName.hpp>

#include <gtest/gtest.h>

using namespace Core;
using namespace Security;

namespace {
    const SPrincipal FLATPAK = {.identity = {.kind = eIdentityKind::FLATPAK, .id = "org.example.App", .stable = true}};

    CClient          makeClient(ePolicy policy = ePolicy::ENFORCED) {
        return CClient{{}, 1, FLATPAK, policy, Bus::eProviderRole::NONE};
    }
}

TEST(Client, AuthorizesThroughItsPermissionContext) {
    auto client  = makeClient();
    auto context = makeShared<CPermissionContext>();

    EXPECT_EQ(client.authorize("example.settings.theme"), eAuthorization::DENIED);

    context->addGrant("example.settings.all", eGrantLifetime::SESSION);
    client.m_permissions = context;

    EXPECT_EQ(client.authorize("example.settings.theme"), eAuthorization::EXPLICIT);
    EXPECT_TRUE(client.hasPermissions({"example.settings.theme", "example.settings.font"}));
    EXPECT_FALSE(client.hasPermissions({"example.settings.theme", "example.other"}));

    // grants end with the context
    context.reset();
    EXPECT_EQ(client.authorize("example.settings.theme"), eAuthorization::DENIED);
}

TEST(Client, InspectsObjectsItCanSee) {
    auto               client  = makeClient();
    auto               context = makeShared<CPermissionContext>();
    const Bus::SObject PUBLIC = {}, PRIVATE = {.requirements = {"example.secret"}};

    client.m_permissions = context;

    EXPECT_TRUE(client.canInspect(PUBLIC));
    EXPECT_FALSE(client.canInspect(PRIVATE));

    context->addGrant(std::string{PERMISSION_ALL_BUS_OBJECTS}, eGrantLifetime::SESSION);
    EXPECT_TRUE(client.canInspect(PRIVATE));
    EXPECT_FALSE(client.hasPermissions(PRIVATE.requirements));

    EXPECT_TRUE(makeClient(ePolicy::BYPASS_ORDINARY).canInspect(PRIVATE));
}

TEST(Client, LetsOnlyHostsDeclareAppIDs) {
    const SPrincipal HOST = {.identity = {.kind = eIdentityKind::HOST_EXECUTABLE, .id = "/usr/bin/python3", .stable = true}};

    CClient          host{{}, 1, HOST, ePolicy::BYPASS_ORDINARY, Bus::eProviderRole::NONE};
    ASSERT_TRUE(host.declareHostApplication("org.example.App"));
    EXPECT_EQ(host.principal().identity, (SAppIdentity{.kind = eIdentityKind::HOST_APPLICATION, .id = "org.example.App", .stable = true}));

    // a sandboxed app keeps its attested identity
    auto sandboxed = makeClient();
    ASSERT_TRUE(sandboxed.declareHostApplication("org.example.Other"));
    EXPECT_EQ(sandboxed.principal().identity, FLATPAK.identity);
}

TEST(Client, FixesItsIdentityOnceSealed) {
    CClient host{{}, 1, {.identity = {.kind = eIdentityKind::HOST_EXECUTABLE, .id = "/usr/bin/app", .stable = true}}, ePolicy::BYPASS_ORDINARY, Bus::eProviderRole::NONE};

    host.seal();
    EXPECT_FALSE(host.declareHostApplication("org.example.App"));
    EXPECT_EQ(host.principal().identity.kind, eIdentityKind::HOST_EXECUTABLE);

    auto twice = makeClient();
    ASSERT_TRUE(twice.declareHostApplication("org.example.App"));
    EXPECT_FALSE(twice.declareHostApplication("org.example.App"));
}

TEST(Client, SnapshotsExplicitGrantsOnly) {
    auto client  = makeClient(ePolicy::BYPASS_ORDINARY);
    auto context = makeShared<CPermissionContext>();

    context->m_display = {.name = "Example"};
    context->addGrant("example.settings", eGrantLifetime::PERMANENT);
    client.m_permissions = context;

    const auto SNAPSHOT = client.snapshot();

    EXPECT_EQ(SNAPSHOT.policy, ePolicy::BYPASS_ORDINARY);
    EXPECT_EQ(SNAPSHOT.display.name, "Example");
    EXPECT_EQ(SNAPSHOT.grants, std::vector<std::string>{"example.settings"});
    EXPECT_EQ(SNAPSHOT.principal.identity, FLATPAK.identity);
}
