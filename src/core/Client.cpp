#include "Client.hpp"

#include <common/PermissionName.hpp>

#include <algorithm>

using namespace Core;

CClient::CClient(WP<Hyprwire::IServerClient> wire, uint64_t id, Security::SPrincipal principal, Security::ePolicy policy, Bus::eProviderRole role) :
    m_wire(std::move(wire)), m_id(id), m_principal(std::move(principal)), m_policy(policy), m_role(role) {
    ;
}

uint64_t CClient::id() const {
    return m_id;
}

const Security::SPrincipal& CClient::principal() const {
    return m_principal;
}

Security::ePolicy CClient::policy() const {
    return m_policy;
}

Bus::eProviderRole CClient::role() const {
    return m_role;
}

SP<Hyprwire::IServerClient> CClient::wire() const {
    return m_wire.lock();
}

Security::eAuthorization CClient::authorize(std::string_view permission) const {
    const auto CONTEXT  = m_permissions.lock();
    const bool EXPLICIT = CONTEXT && CONTEXT->grantFor(permission);

    return Security::authorize(m_principal, m_policy, permission, EXPLICIT);
}

bool CClient::hasPermissions(const std::vector<std::string>& permissions) const {
    return std::ranges::all_of(permissions, [this](const auto& permission) { return authorize(permission) != Security::eAuthorization::DENIED; });
}

bool CClient::canInspect(const Bus::SObject& object) const {
    return hasPermissions(object.requirements) || authorize(Security::PERMISSION_ALL_BUS_OBJECTS) != Security::eAuthorization::DENIED;
}

Bus::SPrincipalSnapshot CClient::snapshot() const {
    Bus::SPrincipalSnapshot snapshot = {.principal = m_principal, .policy = m_policy};

    if (const auto CONTEXT = m_permissions.lock()) {
        snapshot.display = CONTEXT->m_display;
        snapshot.grants  = CONTEXT->grants();
    }

    return snapshot;
}
