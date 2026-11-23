#include <hyprtavern-client/BusObject.hpp>

#include <common/PermissionName.hpp>

#include <gtest/gtest.h>

using namespace Hyprtavern;
using namespace Security;

TEST(Principal, MatchesExplicitGrants) {
    const SPrincipal PRINCIPAL = {.grants = {"example.settings.all", "example.theme"}};

    EXPECT_TRUE(PRINCIPAL.has("example.theme"));
    EXPECT_TRUE(PRINCIPAL.has("example.settings.theme.dark"));
    EXPECT_FALSE(PRINCIPAL.has("example.settings"));
    EXPECT_FALSE(PRINCIPAL.has("example.other"));
}

TEST(Principal, BypassesOnlyValidOrdinaryPermissions) {
    const SPrincipal PRINCIPAL = {.bypassOrdinary = true};

    EXPECT_TRUE(PRINCIPAL.has("example.anything"));
    EXPECT_FALSE(PRINCIPAL.has("not a permission"));
    EXPECT_FALSE(PRINCIPAL.has("core.settings.basic.future"));
    EXPECT_FALSE(PRINCIPAL.has(PERMISSION_TAVERNKEEP));
    EXPECT_FALSE(PRINCIPAL.has(PERMISSION_UNSANDBOXED));
}

TEST(Principal, DerivesUnobtainiumPermissions) {
    const SPrincipal INTERNAL = {.internal = true};
    const SPrincipal HOST     = {.identity = {.kind = eIdentityKind::HOST_EXECUTABLE, .id = "/usr/bin/app", .stable = true}};
    const SPrincipal DECLARED = {.identity = {.kind = eIdentityKind::HOST_APPLICATION, .id = "org.example.App", .stable = true}};
    const SPrincipal GRANTED  = {.grants = {std::string{PERMISSION_TAVERNKEEP}, std::string{PERMISSION_UNSANDBOXED}, "core.unobtainium.all"}};

    EXPECT_TRUE(INTERNAL.has(PERMISSION_TAVERNKEEP));
    EXPECT_FALSE(INTERNAL.has(PERMISSION_UNSANDBOXED));

    EXPECT_TRUE(HOST.has(PERMISSION_UNSANDBOXED));
    EXPECT_FALSE(HOST.has(PERMISSION_TAVERNKEEP));
    EXPECT_TRUE(DECLARED.has(PERMISSION_UNSANDBOXED));

    // never by an explicit grant or a catch-all
    EXPECT_FALSE(GRANTED.has(PERMISSION_TAVERNKEEP));
    EXPECT_FALSE(GRANTED.has(PERMISSION_UNSANDBOXED));
}
