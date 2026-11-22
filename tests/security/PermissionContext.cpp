#include <security/PermissionContext.hpp>

#include <gtest/gtest.h>

#include <format>

using namespace Security;

TEST(PermissionContext, ReportsTheLongestMatchingLifetime) {
    CPermissionContext context;

    EXPECT_EQ(context.grantFor("example.settings.theme"), std::nullopt);

    context.addGrant("example.settings.theme", eGrantLifetime::SESSION);
    EXPECT_EQ(context.grantFor("example.settings.theme"), eGrantLifetime::SESSION);

    context.addGrant("example.settings.all", eGrantLifetime::PERMANENT);
    EXPECT_EQ(context.grantFor("example.settings.theme"), eGrantLifetime::PERMANENT);
    EXPECT_EQ(context.grantFor("example.settings"), std::nullopt);
}

TEST(PermissionContext, UpgradesButNeverDowngradesGrants) {
    CPermissionContext context;

    context.addGrant("example.a", eGrantLifetime::PERMANENT);
    context.addGrant("example.a", eGrantLifetime::SESSION);
    context.addGrant("example.b", eGrantLifetime::SESSION);
    context.addGrant("example.b", eGrantLifetime::PERMANENT);

    EXPECT_EQ(context.permanentGrants(), (std::vector<std::string>{"example.a", "example.b"}));
    EXPECT_EQ(context.grants().size(), 2);
}

TEST(PermissionContext, BoundsDistinctGrants) {
    CPermissionContext context;

    for (size_t i = 0; i < MAX_GRANTS; ++i) {
        ASSERT_TRUE(context.addGrant(std::format("example.p{}", i), eGrantLifetime::SESSION));
    }

    EXPECT_FALSE(context.addGrant("example.overflow", eGrantLifetime::SESSION));
    EXPECT_TRUE(context.addGrant("example.p0", eGrantLifetime::PERMANENT));
    EXPECT_EQ(context.grants().size(), MAX_GRANTS);
}

TEST(PermissionContext, RestoresAllOrNothing) {
    CPermissionContext context;

    for (size_t i = 0; i < MAX_GRANTS - 1; ++i) {
        context.addGrant(std::format("example.p{}", i), eGrantLifetime::SESSION);
    }

    EXPECT_FALSE(context.restore({"example.x", "example.y"}));
    EXPECT_EQ(context.permanentGrants().size(), 0);

    EXPECT_TRUE(context.restore({"example.p0", "example.x"}));
    EXPECT_EQ(context.permanentGrants(), (std::vector<std::string>{"example.p0", "example.x"}));
}

TEST(PermissionContext, AllowsOnePendingRequestPerPermission) {
    CPermissionContext context;

    EXPECT_TRUE(context.beginRequest("example.a"));
    EXPECT_FALSE(context.beginRequest("example.a"));
    EXPECT_TRUE(context.beginRequest("example.b"));

    context.endRequest("example.a");
    EXPECT_TRUE(context.beginRequest("example.a"));
}
