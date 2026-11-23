#include <security/Policy.hpp>

#include <common/PermissionName.hpp>

#include <gtest/gtest.h>

using namespace Security;

namespace {
    const SPrincipal HOST     = {.identity = {.kind = eIdentityKind::HOST_EXECUTABLE, .id = "/usr/bin/example", .stable = true}};
    const SPrincipal FLATPAK  = {.identity = {.kind = eIdentityKind::FLATPAK, .id = "org.example.App", .stable = true}};
    const SPrincipal INTERNAL = {.authority = eAuthority::INTERNAL, .identity = {.kind = eIdentityKind::INTERNAL, .id = "hyprtavern-kv", .stable = true}};
}

TEST(Policy, BypassesVerifiedHostsUnlessEnforced) {
    EXPECT_EQ(policyFor(HOST, false), ePolicy::BYPASS_ORDINARY);
    EXPECT_EQ(policyFor(HOST, true), ePolicy::ENFORCED);
    EXPECT_EQ(policyFor(FLATPAK, false), ePolicy::ENFORCED);
    EXPECT_EQ(policyFor(SPrincipal{}, false), ePolicy::ENFORCED);
    EXPECT_EQ(policyFor(INTERNAL, true), ePolicy::BYPASS_ORDINARY);
}

TEST(Policy, AuthorizesOrdinaryPermissions) {
    EXPECT_EQ(authorize(FLATPAK, ePolicy::ENFORCED, "example.settings", false), eAuthorization::DENIED);
    EXPECT_EQ(authorize(FLATPAK, ePolicy::ENFORCED, "example.settings", true), eAuthorization::EXPLICIT);
    EXPECT_EQ(authorize(HOST, ePolicy::BYPASS_ORDINARY, "example.settings", false), eAuthorization::POLICY);
    EXPECT_EQ(authorize(HOST, ePolicy::BYPASS_ORDINARY, "example.settings", true), eAuthorization::EXPLICIT);
}

TEST(Policy, DerivesHostStatusIndependentlyOfBypass) {
    EXPECT_EQ(authorize(HOST, ePolicy::ENFORCED, PERMISSION_UNSANDBOXED, false), eAuthorization::POLICY);
    EXPECT_EQ(authorize(FLATPAK, ePolicy::BYPASS_ORDINARY, PERMISSION_UNSANDBOXED, true), eAuthorization::DENIED);
    EXPECT_EQ(authorize(INTERNAL, ePolicy::BYPASS_ORDINARY, PERMISSION_UNSANDBOXED, false), eAuthorization::DENIED);
}

TEST(Policy, NeverSatisfiesUnknownCorePermissions) {
    EXPECT_EQ(authorize(HOST, ePolicy::BYPASS_ORDINARY, "core.settings.basic.future", true), eAuthorization::DENIED);
    EXPECT_EQ(authorize(INTERNAL, ePolicy::BYPASS_ORDINARY, "core.settings.basic.future", false), eAuthorization::DENIED);
}

TEST(Policy, ReservesTavernkeepForInternalAuthority) {
    EXPECT_EQ(authorize(INTERNAL, ePolicy::BYPASS_ORDINARY, PERMISSION_TAVERNKEEP, false), eAuthorization::POLICY);
    EXPECT_EQ(authorize(HOST, ePolicy::BYPASS_ORDINARY, PERMISSION_TAVERNKEEP, true), eAuthorization::DENIED);
}
