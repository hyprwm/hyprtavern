#include "Policy.hpp"

#include <common/PermissionName.hpp>

using namespace Security;

ePolicy Security::policyFor(const SPrincipal& principal, bool enforcePermissions) {
    if (principal.authority == eAuthority::INTERNAL)
        return ePolicy::BYPASS_ORDINARY;

    if (!enforcePermissions && principal.identity.kind == eIdentityKind::HOST_EXECUTABLE)
        return ePolicy::BYPASS_ORDINARY;

    return ePolicy::ENFORCED;
}

eAuthorization Security::authorize(const SPrincipal& principal, ePolicy policy, std::string_view permission, bool explicitlyGranted) {
    // never by grants, catch-alls or bypass
    if (!isKnownPermission(permission))
        return eAuthorization::DENIED;

    // derived permissions are never granted explicitly, nor by bypass
    if (permission == PERMISSION_TAVERNKEEP)
        return principal.authority == eAuthority::INTERNAL ? eAuthorization::POLICY : eAuthorization::DENIED;

    if (permission == PERMISSION_UNSANDBOXED)
        return principal.identity.kind == eIdentityKind::HOST_EXECUTABLE ? eAuthorization::POLICY : eAuthorization::DENIED;

    if (explicitlyGranted)
        return eAuthorization::EXPLICIT;

    return policy == ePolicy::BYPASS_ORDINARY ? eAuthorization::POLICY : eAuthorization::DENIED;
}
