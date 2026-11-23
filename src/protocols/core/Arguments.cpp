#include "Arguments.hpp"

#include <common/Limits.hpp>
#include <common/PermissionName.hpp>
#include <common/Wire.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

#include <algorithm>

bool Arguments::validArray(const std::vector<const char*>& array) {
    return array.size() <= MAX_ARRAY_ELEMENTS;
}

std::expected<std::vector<std::string>, uint32_t> Arguments::permissions(const std::vector<const char*>& permissions) {
    if (!validArray(permissions))
        return std::unexpected(HP_HYPRTAVERN_CORE_V1_PROTOCOL_ERRORS_INVALID_ARGUMENT);

    if (!std::ranges::all_of(permissions, [](const char* permission) { return Security::isValidPermission(permission); }))
        return std::unexpected(HP_HYPRTAVERN_CORE_V1_PROTOCOL_ERRORS_INVALID_PERMISSION);

    return Wire::toStrings(permissions);
}
