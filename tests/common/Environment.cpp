#include <common/Environment.hpp>

#include <gtest/gtest.h>

TEST(Environment, AcceptsValidUpdates) {
    EXPECT_TRUE(Environment::validateUpdate({}, {}));
    EXPECT_TRUE(Environment::validateUpdate({"WAYLAND_DISPLAY", "DISPLAY"}, {"wayland-1", ""}));
}

TEST(Environment, RejectsMalformedUpdates) {
    EXPECT_FALSE(Environment::validateUpdate({"WAYLAND_DISPLAY"}, {}));
    EXPECT_FALSE(Environment::validateUpdate({""}, {"x"}));
    EXPECT_FALSE(Environment::validateUpdate({"A=B"}, {"x"}));
    EXPECT_FALSE(Environment::validateUpdate({"A", "A"}, {"x", "y"}));
}

TEST(Environment, RejectsOversizedArrays) {
    std::vector<std::string> storage;
    std::vector<const char*> names, values;

    for (size_t i = 0; i < 10000; ++i) {
        storage.emplace_back(std::format("VAR_{}", i));
    }

    for (const auto& name : storage) {
        names.emplace_back(name.c_str());
        values.emplace_back("x");
    }

    EXPECT_FALSE(Environment::validateUpdate(names, values));

    names.pop_back();
    values.pop_back();

    EXPECT_TRUE(Environment::validateUpdate(names, values));
}
