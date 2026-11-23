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

TEST(SavedContext, KeysByOwner) {
    EXPECT_EQ(savedContextKey(CONTEXT.owner), "context:2:org.example.App");

    // keys allow only some characters, so the others are escaped
    EXPECT_EQ(savedContextKey({.kind = eIdentityKind::HOST_EXECUTABLE, .id = "/opt/my app/a_b?", .stable = true}), "context:1:/opt/my?20app/a?5fb?3f");
    EXPECT_NE(savedContextKey({.kind = eIdentityKind::FLATPAK, .id = "a?20b", .stable = true}), savedContextKey({.kind = eIdentityKind::FLATPAK, .id = "a b", .stable = true}));

    EXPECT_FALSE(savedContextKey({.kind = eIdentityKind::HOST_EXECUTABLE, .id = std::string(1024, 'a'), .stable = true}));
}

TEST(SavedContext, KeepsGrantsFromNewerCatalogs) {
    EXPECT_TRUE(parseSavedContext(withGrants({"core.settings.basic.future"})));
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
