#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace Arguments {
    bool validArray(const std::vector<const char*>& array);

    // canonical permission names, or the protocol error for an oversized array or an invalid name
    std::expected<std::vector<std::string>, uint32_t> permissions(const std::vector<const char*>& permissions);
}
