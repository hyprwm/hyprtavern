#pragma once

#include "../helpers/Memory.hpp"

#include <hp_hyprtavern_core_v1-server.hpp>

#include <cstdint>
#include <vector>

namespace Security {
    struct SSecurityPolicy {
        // Same-euid, verified host processes bypass ordinary permissions by default.
        bool nonSandboxedAppsBypassPermissions = true;
    };

    bool                  isKnownPermission(uint32_t permission);
    bool                  isExternallyRequestablePermission(uint32_t permission);
    bool                  isKnownPermissionMode(uint32_t mode);
    bool                  permissionImplies(uint32_t held, uint32_t requested);
    bool                  permissionListHas(const std::vector<uint32_t>& held, uint32_t requested);
    std::vector<uint32_t> sanitizeExternalPermissions(const std::vector<uint32_t>& permissions);
    std::vector<uint32_t> externallyRequestablePermissionGroups();
}
