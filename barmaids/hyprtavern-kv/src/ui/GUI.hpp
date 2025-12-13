#pragma once

#include <expected>
#include <string>

#include <hyprtoolkit/core/Backend.hpp>

#include "../helpers/Memory.hpp"

namespace GUI {
    inline SP<Hyprtoolkit::IBackend> backend;

    // expects a password.
    std::expected<std::string, std::string> firstTimeSetup();
    std::expected<std::string, std::string> passwordAsk();
};