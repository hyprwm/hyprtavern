#include "PermissionName.hpp"

#include <algorithm>
#include <array>
#include <vector>

using namespace Security;

namespace {
    constexpr size_t                          MAX_PERMISSION_LENGTH   = 255;
    constexpr size_t                          MAX_PERMISSION_SEGMENTS = 16;

    constexpr std::array<std::string_view, 7> CORE_PERMISSIONS = {
        PERMISSION_TAVERNKEEP,
        PERMISSION_UNSANDBOXED,
        "core.settings.basic.all",
        "core.settings.basic.wallpaper",
        "core.settings.sensitive.manage_secrets",
        "core.settings.sensitive.manage_permissions",
        PERMISSION_ALL_BUS_OBJECTS,
    };

    std::vector<std::string_view> splitSegments(std::string_view name) {
        std::vector<std::string_view> segments;

        while (true) {
            const auto DOT = name.find('.');
            segments.emplace_back(name.substr(0, DOT));

            if (DOT == std::string_view::npos)
                break;

            name.remove_prefix(DOT + 1);
        }

        return segments;
    }

    bool isValidSegment(std::string_view segment) {
        if (segment.empty() || segment.front() < 'a' || segment.front() > 'z')
            return false;

        return std::ranges::all_of(segment, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
    }
}

bool Security::isValidPermission(std::string_view name) {
    if (name.size() > MAX_PERMISSION_LENGTH)
        return false;

    const auto SEGMENTS = splitSegments(name);

    if (SEGMENTS.size() < 2 || SEGMENTS.size() > MAX_PERMISSION_SEGMENTS)
        return false;

    for (size_t i = 0; i < SEGMENTS.size(); ++i) {
        if (!isValidSegment(SEGMENTS[i]))
            return false;

        // all is reserved for the end of a name
        if (SEGMENTS[i] == "all" && i != SEGMENTS.size() - 1)
            return false;
    }

    if (SEGMENTS.front() == "core")
        return std::ranges::contains(CORE_PERMISSIONS, name);

    return true;
}

bool Security::isRequestablePermission(std::string_view name) {
    return isValidPermission(name) && name != PERMISSION_TAVERNKEEP;
}

bool Security::isPromptablePermission(std::string_view name) {
    return isRequestablePermission(name) && name != PERMISSION_UNSANDBOXED;
}

bool Security::permissionGrants(std::string_view held, std::string_view requested) {
    if (requested == PERMISSION_TAVERNKEEP || requested == PERMISSION_UNSANDBOXED)
        return false;

    if (held == requested)
        return true;

    if (!held.ends_with(".all"))
        return false;

    // keep the trailing dot, so matching stays segment-aware
    const auto PARENT = held.substr(0, held.size() - 3);

    return requested.size() > PARENT.size() && requested.starts_with(PARENT);
}
