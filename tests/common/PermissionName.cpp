#include <common/PermissionName.hpp>

#include <gtest/gtest.h>

#include <format>
#include <string>

using namespace Security;

TEST(PermissionName, AcceptsValidNames) {
    EXPECT_TRUE(isValidPermission("example.settings"));
    EXPECT_TRUE(isValidPermission("example.theme-dark_2.x9"));
    EXPECT_TRUE(isValidPermission("example.settings.all"));
    EXPECT_TRUE(isValidPermission("core.settings.basic.wallpaper"));
    EXPECT_TRUE(isValidPermission(PERMISSION_TAVERNKEEP));
}

TEST(PermissionName, RejectsInvalidSyntax) {
    EXPECT_FALSE(isValidPermission(""));
    EXPECT_FALSE(isValidPermission("example"));
    EXPECT_FALSE(isValidPermission("Example.settings"));
    EXPECT_FALSE(isValidPermission("example..settings"));
    EXPECT_FALSE(isValidPermission(".example.settings"));
    EXPECT_FALSE(isValidPermission("example.settings."));
    EXPECT_FALSE(isValidPermission("example.1settings"));
    EXPECT_FALSE(isValidPermission("example.set tings"));
    EXPECT_FALSE(isValidPermission("example.all.settings"));
    EXPECT_FALSE(isValidPermission("all.settings"));
}

TEST(PermissionName, EnforcesLengthLimits) {
    std::string seventeen = "a";
    for (size_t i = 0; i < 16; ++i) {
        seventeen += ".a";
    }

    EXPECT_FALSE(isValidPermission(seventeen));
    EXPECT_TRUE(isValidPermission(seventeen.substr(0, seventeen.size() - 2)));

    EXPECT_TRUE(isValidPermission(std::format("example.{}", std::string(247, 'a'))));
    EXPECT_FALSE(isValidPermission(std::format("example.{}", std::string(248, 'a'))));
}

TEST(PermissionName, ReservesTheCoreNamespace) {
    EXPECT_FALSE(isValidPermission("core.settings"));
    EXPECT_FALSE(isValidPermission("core.settings.all"));
    EXPECT_FALSE(isValidPermission("core.unobtainium.all"));
    EXPECT_TRUE(isValidPermission("corex.settings"));
}

TEST(PermissionName, ClassifiesRequestability) {
    EXPECT_FALSE(isRequestablePermission(PERMISSION_TAVERNKEEP));
    EXPECT_TRUE(isRequestablePermission(PERMISSION_UNSANDBOXED));
    EXPECT_FALSE(isPromptablePermission(PERMISSION_UNSANDBOXED));
    EXPECT_TRUE(isPromptablePermission("example.settings"));
    EXPECT_FALSE(isPromptablePermission("example"));
}

TEST(PermissionName, MatchesSegmentAwareCatchAlls) {
    EXPECT_TRUE(permissionGrants("example.settings", "example.settings"));
    EXPECT_TRUE(permissionGrants("example.settings.all", "example.settings.theme"));
    EXPECT_TRUE(permissionGrants("example.settings.all", "example.settings.theme.dark"));
    EXPECT_TRUE(permissionGrants("example.settings.all", "example.settings.theme.all"));
    EXPECT_TRUE(permissionGrants("example.settings.all", "example.settings.all"));
    EXPECT_FALSE(permissionGrants("example.settings.all", "example.settings"));
    EXPECT_FALSE(permissionGrants("example.settings.all", "example.setting.theme"));
    EXPECT_FALSE(permissionGrants("example.settings.all", "example.settingsx.theme"));
    EXPECT_FALSE(permissionGrants("example.settings", "example.settings.theme"));
}

TEST(PermissionName, NeverGrantsDerivedPermissions) {
    EXPECT_FALSE(permissionGrants(PERMISSION_TAVERNKEEP, PERMISSION_TAVERNKEEP));
    EXPECT_FALSE(permissionGrants(PERMISSION_UNSANDBOXED, PERMISSION_UNSANDBOXED));
    EXPECT_FALSE(permissionGrants("core.settings.basic.all", PERMISSION_UNSANDBOXED));
}
