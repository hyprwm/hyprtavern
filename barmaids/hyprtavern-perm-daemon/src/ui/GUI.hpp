#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include <hyprtoolkit/core/Backend.hpp>
#include <hp_hyprtavern_permission_authentication_v1-server.hpp>

#include "../helpers/Memory.hpp"

namespace GUI {
    enum ePermissionChoice : uint8_t {
        PERMISSION_CHOICE_DENY,
        PERMISSION_CHOICE_ALLOW_ONCE,
        PERMISSION_CHOICE_ALLOW_ALWAYS,
    };
    inline SP<Hyprtoolkit::IBackend>              backend;

    inline bool                                   available = false;

    void                                          updateEnv();

    std::expected<ePermissionChoice, std::string> permissionAsk(const std::string& appName, const std::string& appID, uint32_t permission,
                                                                hpHyprtavernPermissionAuthenticationV1AskType askType);
};