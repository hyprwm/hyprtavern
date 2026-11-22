#include "Arguments.hpp"

#include <common/Limits.hpp>
#include <common/PermissionName.hpp>
#include <common/Wire.hpp>

#include <algorithm>

bool Arguments::validArray(const std::vector<const char*>& array) {
    return array.size() <= MAX_ARRAY_ELEMENTS;
}

std::optional<std::vector<std::string>> Arguments::permissions(const std::vector<const char*>& permissions) {
    if (!validArray(permissions) || !std::ranges::all_of(permissions, [](const char* permission) { return Security::isValidPermission(permission); }))
        return std::nullopt;

    return Wire::toStrings(permissions);
}
