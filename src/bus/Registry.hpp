#pragma once

#include "../security/Identity.hpp"

#include <common/Memory.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Bus {
    constexpr size_t           MAX_OBJECTS    = 9999;
    constexpr size_t           MAX_PROTOCOLS  = 9999;
    constexpr size_t           MAX_PROPERTIES = 9999;

    constexpr std::string_view PROTOCOL_BARMAID                   = "hp_hyprtavern_barmaid_v1";
    constexpr std::string_view PROTOCOL_KV_STORE                  = "hp_hyprtavern_kv_store_v1";
    constexpr std::string_view PROTOCOL_PERMISSION_AUTHENTICATION = "hp_hyprtavern_permission_authentication_v1";

    // infrastructure role of a provider, established by the tavern when starting a barmaid
    enum class eProviderRole : uint8_t {
        NONE,
        KV_STORE,
        PERMISSION_AUTHENTICATION,
    };

    enum class eRegistryError : uint8_t {
        LIMIT_REACHED,
        ALREADY_EXPOSED,
        UNAUTHORIZED_PROVIDER,
        INVALID_REGISTRATION,
    };

    // values match bus_query_filter_mode in hp_hyprtavern_core_v1
    enum class eFilterMode : uint8_t {
        ALL = 0,
        ANY = 1,
    };

    struct SProtocol {
        std::string              name;
        uint32_t                 version = 0;
        std::vector<std::string> requirements;
        bool                     exclusive = false;
    };

    struct SObject {
        uint32_t                           id    = 0;
        uint64_t                           owner = 0;
        Security::SAppIdentity             provider;
        std::string                        name;
        std::vector<std::string>           requirements;
        std::vector<SProtocol>             protocols;
        std::map<std::string, std::string> properties;

        const SProtocol*                   protocol(std::string_view name) const;
    };

    struct SQuery {
        std::vector<std::string>                         protocols;
        eFilterMode                                      protocolMode = eFilterMode::ALL;
        std::vector<std::pair<std::string, std::string>> properties;
        eFilterMode                                      propertyMode = eFilterMode::ALL;
    };

    // (group):(name), both nonempty and made of letters, numbers, _ or +
    bool isValidPropertyName(std::string_view name);

    // splits a query property at its first '=', validating the name
    std::optional<std::pair<std::string, std::string>> parseQueryProperty(std::string_view property);

    // all live bus objects. Objects are owned by client connections, identified by an opaque owner id.
    class CRegistry {
      public:
        std::expected<uint32_t, eRegistryError> add(uint64_t owner, Security::SAppIdentity provider, std::string name, std::vector<std::string> requirements);
        void                                    remove(uint32_t id);
        const SObject*                          get(uint32_t id) const;

        // atomically adds or replaces a protocol registration. Failures leave the object unchanged.
        std::expected<void, eRegistryError> exposeProtocol(uint32_t id, eProviderRole role, SProtocol protocol);

        // an empty value removes the property.
        std::expected<void, eRegistryError> exposeProperty(uint32_t id, const std::string& name, const std::string& value);

        void                                setRequirements(uint32_t id, std::vector<std::string> requirements);

        // ids of matching objects for which visible returns true, in ascending order.
        std::vector<uint32_t> query(const SQuery& query, const std::function<bool(const SObject&)>& visible) const;

      private:
        std::map<uint32_t, SObject> m_objects;
        uint32_t                    m_nextID = 1;
    };

    inline UP<CRegistry> registry;
}
