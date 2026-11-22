#include "Principal.hpp"
#include "Helpers.hpp"

#include <common/PermissionName.hpp>
#include <common/Wire.hpp>

#include <algorithm>

using namespace Hyprtavern;

bool SPrincipal::has(std::string_view permission) const {
    // derived permissions are never granted explicitly, nor by bypass
    if (permission == Security::PERMISSION_TAVERNKEEP)
        return internal;

    if (permission == Security::PERMISSION_UNSANDBOXED)
        return identity.kind == eIdentityKind::HOST_EXECUTABLE;

    if (!Security::isValidPermission(permission))
        return false;

    if (bypassOrdinary)
        return true;

    return std::ranges::any_of(grants, [permission](const auto& grant) { return Security::permissionGrants(grant, permission); });
}

CPrincipalRedemption::CPrincipalRedemption(const SP<CCHpHyprtavernCoreManagerV1Object>& manager, const std::string& token, DoneFn&& done) :
    m_principal(makeShared<CCHpHyprtavernConnectionPrincipalV1Object>(manager->sendRedeemConnectionPrincipalToken(token.c_str()))), m_done(std::move(done)) {
    m_principal->setAuthority([this](uint32_t authority, uint32_t policy) {
        m_data.internal       = authority == HP_HYPRTAVERN_CORE_V1_PRINCIPAL_AUTHORITY_INTERNAL;
        m_data.bypassOrdinary = policy == HP_HYPRTAVERN_CORE_V1_PERMISSION_POLICY_BYPASS_ORDINARY_PERMISSIONS;
    });

    m_principal->setProcessIdentity([this](uint32_t pid) { m_data.pid = pid; });
    m_principal->setApplicationIdentity([this](const char* id, uint32_t kind, uint32_t stable) { m_data.identity = identityFromWire(id, kind, stable); });

    m_principal->setDisplayIdentity([this](const char* name, const char* description) {
        m_data.displayName        = name;
        m_data.displayDescription = description;
    });

    m_principal->setExplicitPermissionGrants([this](const std::vector<const char*>& grants) { m_data.grants = Wire::toStrings(grants); });

    m_principal->setDone([this] { finish(true); });
    m_principal->setFailed([this] { finish(false); });
}

bool CPrincipalRedemption::done() const {
    return m_finished;
}

void CPrincipalRedemption::finish(bool redeemed) {
    if (redeemed)
        m_done(std::move(m_data));
    else
        m_done(std::nullopt);

    m_principal->sendDestroy();

    // last, so the principal is never dropped while its callback runs
    m_finished = true;
}
