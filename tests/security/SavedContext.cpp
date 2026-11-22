#include <security/SavedContext.hpp>

#include <common/PermissionName.hpp>

#include <gtest/gtest.h>

#include <format>

using namespace Security;

namespace {
    const SSavedContext CONTEXT = {
        .owner   = {.kind = eIdentityKind::FLATPAK, .id = "org.example.App", .stable = true},
        .display = {.name = "Example", .description = "An \"example\" app"},
        .grants  = {"example.settings.all", "core.settings.basic.wallpaper"},
    };

    std::string withGrants(const std::vector<std::string>& grants) {
        auto context   = CONTEXT;
        context.grants = grants;
        return serializeSavedContext(context);
    }
}

TEST(SavedContext, RoundTrips) {
    const auto PARSED = parseSavedContext(serializeSavedContext(CONTEXT));

    ASSERT_TRUE(PARSED);
    EXPECT_EQ(PARSED->owner, CONTEXT.owner);
    EXPECT_EQ(PARSED->display, CONTEXT.display);
    EXPECT_EQ(PARSED->grants, CONTEXT.grants);
}

TEST(SavedContext, RejectsMalformedRecords) {
    EXPECT_FALSE(parseSavedContext(""));
    EXPECT_FALSE(parseSavedContext("{\"version\":2,\"ownerKind\":2,\"ownerId\":\"org.example.App\",\"displayName\":\"\",\"displayDescription\":\"\",\"grants\":[]}"));
    EXPECT_FALSE(parseSavedContext("{\"version\":1,\"ownerKind\":7,\"ownerId\":\"org.example.App\",\"displayName\":\"\",\"displayDescription\":\"\",\"grants\":[]}"));
    EXPECT_FALSE(parseSavedContext("{\"version\":1,\"ownerKind\":0,\"ownerId\":\"\",\"displayName\":\"\",\"displayDescription\":\"\",\"grants\":[]}"));
}

TEST(SavedContext, RejectsInvalidGrants) {
    EXPECT_FALSE(parseSavedContext(withGrants({"example"})));
    EXPECT_FALSE(parseSavedContext(withGrants({"example.a", "example.a"})));
    EXPECT_FALSE(parseSavedContext(withGrants({std::string{PERMISSION_UNSANDBOXED}})));
    EXPECT_FALSE(parseSavedContext(withGrants({std::string{PERMISSION_TAVERNKEEP}})));

    std::vector<std::string> tooMany;
    for (size_t i = 0; i <= MAX_GRANTS; ++i) {
        tooMany.emplace_back(std::format("example.p{}", i));
    }

    EXPECT_FALSE(parseSavedContext(withGrants(tooMany)));
}
