#include "Registry.hpp"

#include <common/PermissionName.hpp>

#include <algorithm>

using namespace Bus;

namespace {
    template <typename T, typename Pred>
    bool matchesFilter(const std::vector<T>& filters, eFilterMode mode, Pred&& pred) {
        if (filters.empty())
            return true;

        if (mode == eFilterMode::ALL)
            return std::ranges::all_of(filters, pred);

        return std::ranges::any_of(filters, pred);
    }

    std::expected<void, eRegistryError> checkInfrastructure(eProviderRole role, const SProtocol& protocol) {
        // private to the barmaid startup channels
        if (protocol.name == PROTOCOL_BARMAID)
            return std::unexpected(eRegistryError::UNAUTHORIZED_PROVIDER);

        if (protocol.name == PROTOCOL_KV_STORE) {
            if (role != eProviderRole::KV_STORE)
                return std::unexpected(eRegistryError::UNAUTHORIZED_PROVIDER);

            if (!protocol.exclusive)
                return std::unexpected(eRegistryError::INVALID_REGISTRATION);
        }

        if (protocol.name == PROTOCOL_PERMISSION_AUTHENTICATION) {
            if (role != eProviderRole::PERMISSION_AUTHENTICATION)
                return std::unexpected(eRegistryError::UNAUTHORIZED_PROVIDER);

            if (!protocol.exclusive || !std::ranges::contains(protocol.requirements, Security::PERMISSION_TAVERNKEEP))
                return std::unexpected(eRegistryError::INVALID_REGISTRATION);
        }

        return {};
    }
}

const SProtocol* SObject::protocol(std::string_view name) const {
    const auto IT = protocols.find(name);
    return IT == protocols.end() ? nullptr : &IT->second;
}

std::optional<std::pair<std::string, std::string>> Bus::parseQueryProperty(std::string_view property) {
    const auto EQUALS = property.find('=');
    if (EQUALS == std::string_view::npos || !isValidPropertyName(property.substr(0, EQUALS)))
        return std::nullopt;

    return std::pair{std::string{property.substr(0, EQUALS)}, std::string{property.substr(EQUALS + 1)}};
}

std::expected<uint32_t, eRegistryError> CRegistry::add(uint64_t owner, Security::SAppIdentity provider, std::string name, std::vector<std::string> requirements) {
    // ids identify a single registration, and are never reused
    if (m_objects.size() >= MAX_OBJECTS || m_nextID == 0)
        return std::unexpected(eRegistryError::LIMIT_REACHED);

    const auto ID = m_nextID++;

    m_objects.emplace(ID,
                      SObject{
                          .id           = ID,
                          .owner        = owner,
                          .provider     = std::move(provider),
                          .name         = std::move(name),
                          .requirements = std::move(requirements),
                      });

    return ID;
}

void CRegistry::remove(uint32_t id) {
    const auto IT = m_objects.find(id);
    if (IT == m_objects.end())
        return;

    for (const auto& [name, protocol] : IT->second.protocols) {
        const auto PROVIDERS = m_providers.find(name);
        PROVIDERS->second.erase(id);

        if (PROVIDERS->second.empty())
            m_providers.erase(PROVIDERS);
    }

    m_objects.erase(IT);
}

const SObject* CRegistry::get(uint32_t id) const {
    const auto IT = m_objects.find(id);
    return IT == m_objects.end() ? nullptr : &IT->second;
}

std::expected<void, eRegistryError> CRegistry::exposeProtocol(uint32_t id, eProviderRole role, SProtocol protocol) {
    const auto IT = m_objects.find(id);
    if (IT == m_objects.end() || protocol.name.empty() || protocol.version == 0)
        return std::unexpected(eRegistryError::INVALID_REGISTRATION);

    if (const auto RET = checkInfrastructure(role, protocol); !RET)
        return RET;

    if (const auto PROVIDERS = m_providers.find(protocol.name); PROVIDERS != m_providers.end()) {
        for (const auto& [otherID, exclusive] : PROVIDERS->second) {
            if (otherID == id)
                continue;

            if (exclusive || protocol.exclusive)
                return std::unexpected(eRegistryError::ALREADY_EXPOSED);

            // an exclusive registration is always the only one, so the first other provider decides
            break;
        }
    }

    auto& protocols = IT->second.protocols;

    // replacing a registration does not take a slot
    if (!protocols.contains(protocol.name) && protocols.size() >= MAX_PROTOCOLS)
        return std::unexpected(eRegistryError::LIMIT_REACHED);

    m_providers[protocol.name][id] = protocol.exclusive;
    protocols.insert_or_assign(protocol.name, std::move(protocol));

    return {};
}

std::expected<void, eRegistryError> CRegistry::exposeProperty(uint32_t id, const std::string& name, const std::string& value) {
    const auto IT = m_objects.find(id);
    if (IT == m_objects.end())
        return std::unexpected(eRegistryError::INVALID_REGISTRATION);

    auto& properties = IT->second.properties;

    if (value.empty()) {
        properties.erase(name);
        return {};
    }

    if (!properties.contains(name) && properties.size() >= MAX_PROPERTIES)
        return std::unexpected(eRegistryError::LIMIT_REACHED);

    properties[name] = value;
    return {};
}

void CRegistry::setRequirements(uint32_t id, std::vector<std::string> requirements) {
    if (const auto IT = m_objects.find(id); IT != m_objects.end())
        IT->second.requirements = std::move(requirements);
}

std::vector<uint32_t> CRegistry::query(const SQuery& query, const std::function<bool(const SObject&)>& visible) const {
    std::vector<uint32_t> result;

    for (const auto& [id, object] : m_objects) {
        if (!matchesFilter(query.protocols, query.protocolMode, [&object](const auto& name) { return object.protocol(name); }))
            continue;

        const auto HAS_PROPERTY = [&object](const auto& property) {
            const auto IT = object.properties.find(property.first);
            return IT != object.properties.end() && IT->second == property.second;
        };

        if (!matchesFilter(query.properties, query.propertyMode, HAS_PROPERTY))
            continue;

        if (!visible(object))
            continue;

        result.emplace_back(id);
    }

    return result;
}
