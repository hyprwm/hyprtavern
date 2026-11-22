#pragma once

#include <expected>
#include <string>
#include <vector>

namespace Environment {
    // validates a complete update_tavern_environment request. Returns a protocol error message on failure.
    std::expected<void, std::string> validateUpdate(const std::vector<const char*>& names, const std::vector<const char*>& values);

    // applies a validated update to this process. Empty values unset the variable.
    void applyUpdate(const std::vector<const char*>& names, const std::vector<const char*>& values);
}
