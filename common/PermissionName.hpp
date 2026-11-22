#pragma once

#include <string_view>

// permission names and matching rules, see docs/PERMISSIONS.md
namespace Security {
    constexpr std::string_view PERMISSION_TAVERNKEEP      = "core.unobtainium.tavernkeep";
    constexpr std::string_view PERMISSION_UNSANDBOXED     = "core.unobtainium.unsandboxed";
    constexpr std::string_view PERMISSION_ALL_BUS_OBJECTS = "core.monitoring.sensitive.all_bus_objects";

    // syntactically valid, and part of the standard catalog if in the reserved core namespace.
    bool isValidPermission(std::string_view name);

    // valid and externally requestable. Excludes the tavern's internal authority.
    bool isRequestablePermission(std::string_view name);

    // requestable and obtainable through user consent. Excludes derived host status.
    bool isPromptablePermission(std::string_view name);

    // whether an explicit grant for held satisfies requested, including terminal .all catch-alls.
    // derived core.unobtainium permissions are never satisfied by explicit grants.
    bool permissionGrants(std::string_view held, std::string_view requested);
}
