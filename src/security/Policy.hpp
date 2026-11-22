#pragma once

#include "Identity.hpp"

#include <string_view>

namespace Security {
    // values match principal_authority in hp_hyprtavern_core_v1
    enum class eAuthority : uint8_t {
        EXTERNAL = 0,
        INTERNAL = 1,
    };

    // values match permission_policy in hp_hyprtavern_core_v1
    enum class ePolicy : uint8_t {
        ENFORCED        = 0,
        BYPASS_ORDINARY = 1,
    };

    enum class eAuthorization : uint8_t {
        DENIED,
        EXPLICIT,
        POLICY,
    };

    // who a client connection is. Fixed for the lifetime of the connection.
    struct SPrincipal {
        eAuthority   authority = eAuthority::EXTERNAL;
        SAppIdentity identity;
        pid_t        pid = -1;
    };

    // internal components and, unless enforcement is requested, verified hosts bypass ordinary checks.
    ePolicy policyFor(const SPrincipal& principal, bool enforcePermissions);

    // explicitlyGranted tells whether the principal holds an explicit grant matching the permission.
    eAuthorization authorize(const SPrincipal& principal, ePolicy policy, std::string_view permission, bool explicitlyGranted);
}
