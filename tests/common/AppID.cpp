#include <common/AppID.hpp>

#include <gtest/gtest.h>

#include <format>
#include <string>

using namespace Security;

TEST(AppID, AcceptsReverseDNSNames) {
    EXPECT_TRUE(isValidAppID("org.example.App"));
    EXPECT_TRUE(isValidAppID("org.example_2.my-app"));
    EXPECT_TRUE(isValidAppID(std::format("org.{}", std::string(251, 'a'))));
}

TEST(AppID, RejectsMalformedNames) {
    EXPECT_FALSE(isValidAppID(""));
    EXPECT_FALSE(isValidAppID("example"));
    EXPECT_FALSE(isValidAppID("org..example"));
    EXPECT_FALSE(isValidAppID(".org.example"));
    EXPECT_FALSE(isValidAppID("org.example."));
    EXPECT_FALSE(isValidAppID("org.1example"));
    EXPECT_FALSE(isValidAppID("org.exa mple"));
    EXPECT_FALSE(isValidAppID("org/example.App"));
    EXPECT_FALSE(isValidAppID(std::format("org.{}", std::string(252, 'a'))));
}
