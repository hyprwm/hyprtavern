#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Arguments {
    // error code for malformed requests without a dedicated protocol error
    constexpr uint32_t MALFORMED = UINT32_MAX;

    bool               validArray(const std::vector<const char*>& array);

    // canonical permission names, or nullopt if any name is invalid or the array is oversized
    std::optional<std::vector<std::string>> permissions(const std::vector<const char*>& permissions);
}
