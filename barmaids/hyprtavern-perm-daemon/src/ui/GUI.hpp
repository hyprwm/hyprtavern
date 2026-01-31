#pragma once

#include <expected>
#include <string>

#include <hyprtoolkit/core/Backend.hpp>

#include "../helpers/Memory.hpp"

namespace GUI {
    inline SP<Hyprtoolkit::IBackend> backend;

    inline bool                      available = false;

    void                             updateEnv();

    std::expected<bool, std::string> permissionAsk(const std::string& appName, const std::string& appID);
};