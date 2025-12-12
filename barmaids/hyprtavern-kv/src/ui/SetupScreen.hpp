#pragma once

#include <expected>
#include <string>

namespace GUI {
    // expects a password.
    std::expected<std::string, std::string> firstTimeSetup();
};