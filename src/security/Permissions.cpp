#include "Permissions.hpp"

#include <algorithm>
#include <array>

using namespace Security;

namespace {
    constexpr std::array<uint32_t, 13> KNOWN_PERMISSIONS = {
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_UNOBTAINIUM,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_BASIC,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_WALLPAPER,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_SENSITIVE,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_MANAGE_SECRETS,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_MANAGE_PERMISSIONS,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING_BASIC,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING_ALL_BUS_OBJECTS,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MANAGEMENT,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MANAGEMENT_ENVIRONMENT,
    };

    bool permissionIsInRange(uint32_t held, uint32_t requested) {
        if (held == requested)
            return true;

        // The current core permission enum uses N0000 for top-level groups and NN000 for subgroups.
        if (held != 0 && held % 10000 == 0)
            return requested >= held && requested < held + 10000;

        if (held != 0 && held % 1000 == 0)
            return requested >= held && requested < held + 1000;

        return false;
    }
}

bool Security::isKnownPermission(uint32_t permission) {
    return std::ranges::contains(KNOWN_PERMISSIONS, permission);
}

bool Security::isExternallyRequestablePermission(uint32_t permission) {
    return isKnownPermission(permission) && permission != HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_UNOBTAINIUM &&
        permission != HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP;
}

bool Security::isKnownPermissionMode(uint32_t mode) {
    return mode == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_SESSION || mode == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT;
}

bool Security::permissionImplies(uint32_t held, uint32_t requested) {
    if (!isKnownPermission(held) || !isKnownPermission(requested) || requested == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_UNOBTAINIUM)
        return false;

    if (requested == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP)
        return held == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP;

    if (held == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP)
        return true;

    return permissionIsInRange(held, requested);
}

bool Security::permissionListHas(const std::vector<uint32_t>& held, uint32_t requested) {
    return std::ranges::any_of(held, [&requested](const auto& permission) { return permissionImplies(permission, requested); });
}

std::vector<uint32_t> Security::sanitizeExternalPermissions(const std::vector<uint32_t>& permissions) {
    std::vector<uint32_t> sanitized;
    sanitized.reserve(permissions.size());
    for (const auto permission : permissions) {
        if (!isExternallyRequestablePermission(permission) || std::ranges::contains(sanitized, permission))
            continue;
        sanitized.emplace_back(permission);
    }
    return sanitized;
}

std::vector<uint32_t> Security::externallyRequestablePermissionGroups() {
    return {
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING,
        HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MANAGEMENT,
    };
}
