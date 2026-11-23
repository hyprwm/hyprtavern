#include "PermissionContext.hpp"

#include <common/PermissionName.hpp>

using namespace Security;

std::optional<eGrantLifetime> CPermissionContext::grantFor(std::string_view permission) const {
    std::optional<eGrantLifetime> result;

    for (const auto& [name, lifetime] : m_grants) {
        if (!permissionGrants(name, permission))
            continue;

        if (lifetime == eGrantLifetime::PERMANENT)
            return lifetime;

        result = lifetime;
    }

    return result;
}

bool CPermissionContext::addGrant(const std::string& permission, eGrantLifetime lifetime) {
    if (const auto it = m_grants.find(permission); it != m_grants.end()) {
        if (lifetime == eGrantLifetime::PERMANENT)
            it->second = lifetime;

        return true;
    }

    if (m_grants.size() >= MAX_GRANTS)
        return false;

    m_grants.emplace(permission, lifetime);
    return true;
}

bool CPermissionContext::restore(const std::vector<std::string>& permanent) {
    size_t added = 0;

    for (const auto& permission : permanent) {
        if (!m_grants.contains(permission))
            ++added;
    }

    if (m_grants.size() + added > MAX_GRANTS)
        return false;

    for (const auto& permission : permanent) {
        m_grants[permission] = eGrantLifetime::PERMANENT;
    }

    return true;
}

std::vector<std::string> CPermissionContext::grants() const {
    std::vector<std::string> result;
    result.reserve(m_grants.size());

    for (const auto& [name, lifetime] : m_grants) {
        result.emplace_back(name);
    }

    return result;
}

std::vector<std::string> CPermissionContext::permanentGrants() const {
    std::vector<std::string> result;

    for (const auto& [name, lifetime] : m_grants) {
        if (lifetime == eGrantLifetime::PERMANENT)
            result.emplace_back(name);
    }

    return result;
}

size_t CPermissionContext::grantCount() const {
    return m_grants.size();
}

bool CPermissionContext::beginRequest(const std::string& permission) {
    return m_pending.emplace(permission).second;
}

void CPermissionContext::endRequest(const std::string& permission) {
    m_pending.erase(permission);
}

size_t CPermissionContext::pendingCount() const {
    return m_pending.size();
}
