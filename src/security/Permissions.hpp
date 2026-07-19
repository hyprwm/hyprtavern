#pragma once

#include "../helpers/Memory.hpp"

#include <hp_hyprtavern_core_v1-server.hpp>

#include <vector>

namespace Security {
    struct SSecurityPolicy {
        // TODO: wire this to cfg
        bool nonSandboxedAppsBypassPermissions = false;
    };

    bool permissionImplies(uint32_t held, uint32_t requested);
    bool permissionListHas(const std::vector<uint32_t>& held, uint32_t requested);
}