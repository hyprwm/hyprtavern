#include <ui/Text.hpp>

#include <gtest/gtest.h>

using namespace Prompts;

TEST(PromptText, EscapesUntrustedStrings) {
    EXPECT_EQ(Text::escapeMarkup("<b>Bank</b> & 'co'"), "&lt;b&gt;Bank&lt;/b&gt; &amp; &#39;co&#39;");
    EXPECT_EQ(Text::escapeMarkup("line\nbreak\x1b[31m"),
              "line\xEF\xBF\xBD"
              "break\xEF\xBF\xBD[31m");

    // right-to-left override, U+202E
    EXPECT_EQ(Text::escapeMarkup("app\xE2\x80\xAEgpj.exe"), "app\xEF\xBF\xBDgpj.exe");
    EXPECT_EQ(Text::escapeMarkup("Ünïcödé"), "Ünïcödé");
}

TEST(PromptText, ShowsTheAttestedIdentityFirst) {
    const SRequest REQUEST = {.displayName = "<b>Totally Firefox</b>", .attestedID = "org.example.App", .kind = eIdentityKind::FLATPAK, .permission = "example.settings"};

    const auto     BODY = Text::promptBody(REQUEST);

    EXPECT_TRUE(BODY.starts_with("The Flatpak app <b>org.example.App</b>"));
    EXPECT_TRUE(BODY.contains("&lt;b&gt;Totally Firefox&lt;/b&gt;"));
    EXPECT_FALSE(BODY.contains("<b>Totally Firefox</b>"));
    EXPECT_TRUE(BODY.contains("<tt>example.settings</tt>"));
}

TEST(PromptText, FlagsDeclaredIdentities) {
    const SRequest REQUEST = {.attestedID = "org.example.App", .kind = eIdentityKind::HOST_APPLICATION, .permission = "example.settings"};

    EXPECT_TRUE(Text::promptBody(REQUEST).starts_with("An unsandboxed program declaring itself as <b>org.example.App</b>"));
}

TEST(PromptText, FlagsUnverifiedApps) {
    const SRequest REQUEST = {.displayName = "Example", .permission = "example.settings"};

    EXPECT_TRUE(Text::promptBody(REQUEST).starts_with("An <b>unverified app</b>"));
}

TEST(PromptText, ExplainsCatchAlls) {
    const SRequest REQUEST = {.attestedID = "/usr/bin/example", .kind = eIdentityKind::HOST_EXECUTABLE, .permission = "example.settings.all"};

    EXPECT_TRUE(Text::promptBody(REQUEST).contains("every permission under <tt>example.settings</tt>"));
    EXPECT_EQ(Text::permissionTitle("core.settings.basic.wallpaper"), "Manage your wallpaper");
    EXPECT_EQ(Text::permissionTitle("example.settings.all"), "Permission request");
}
