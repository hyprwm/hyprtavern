#include <core/Transaction.hpp>

#include <common/PermissionName.hpp>

#include <gtest/gtest.h>

using namespace Prompts;

namespace {
    constexpr uint32_t SESSION = 0, PERSIST = 1;
    constexpr uint32_t UNKNOWN = 0, FLATPAK = 2;
}

TEST(Transaction, FreezesOnAsk) {
    CTransactionState state;

    ASSERT_TRUE(state.setDisplayName("Example"));
    ASSERT_TRUE(state.setAttestedID("org.example.App", FLATPAK));
    ASSERT_TRUE(state.ask("example.settings", PERSIST));

    EXPECT_TRUE(state.asked());
    EXPECT_EQ(state.request().permission, "example.settings");
    EXPECT_EQ(state.request().kind, eIdentityKind::FLATPAK);
    EXPECT_TRUE(state.request().persist);

    EXPECT_FALSE(state.setDisplayName("Changed"));
    EXPECT_FALSE(state.setAttestedID("org.example.Other", FLATPAK));
    EXPECT_FALSE(state.ask("example.other", SESSION));
    EXPECT_EQ(state.request().displayName, "Example");
}

TEST(Transaction, RequiresAnAttestedIdentity) {
    CTransactionState state;

    EXPECT_FALSE(state.ask("example.settings", SESSION));

    EXPECT_FALSE(state.setAttestedID("", FLATPAK));
    EXPECT_FALSE(state.setAttestedID("org.example.App", UNKNOWN));
    EXPECT_FALSE(state.setAttestedID("org.example.App", 7));

    ASSERT_TRUE(state.setAttestedID("", UNKNOWN));
    EXPECT_TRUE(state.ask("example.settings", SESSION));
}

TEST(Transaction, RejectsUnpromptablePermissions) {
    for (const auto PERMISSION : {Security::PERMISSION_TAVERNKEEP, Security::PERMISSION_UNSANDBOXED, std::string_view{"example"}}) {
        CTransactionState state;
        ASSERT_TRUE(state.setAttestedID("org.example.App", FLATPAK));
        EXPECT_FALSE(state.ask(std::string{PERMISSION}, SESSION));
    }

    CTransactionState state;
    ASSERT_TRUE(state.setAttestedID("org.example.App", FLATPAK));
    EXPECT_FALSE(state.ask("example.settings", 2));
}
