#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Security {
    // session and permanent explicit grants together may contain at most this many names
    constexpr size_t MAX_GRANTS = 512;

    // requests pending on a single context
    constexpr size_t MAX_PENDING_REQUESTS = 512;

    enum class eGrantLifetime : uint8_t {
        SESSION,
        PERMANENT,
    };

    // self-asserted, human-readable strings. Never used for authorization.
    struct SDisplayIdentity {
        std::string name;
        std::string description;

        bool        operator==(const SDisplayIdentity&) const = default;
    };

    // explicit grants and pending requests of a permission context
    class CPermissionContext {
      public:
        // lifetime of the explicit grant satisfying permission, if any. Permanent grants win.
        std::optional<eGrantLifetime> grantFor(std::string_view permission) const;

        // fails without changes when the grant limit would be exceeded.
        bool addGrant(const std::string& permission, eGrantLifetime lifetime);

        // merges restored permanent grants, all or nothing.
        bool                     restore(const std::vector<std::string>& permanent);

        std::vector<std::string> grants() const;
        std::vector<std::string> permanentGrants() const;
        size_t                   grantCount() const;

        // at most one request per permission may be pending.
        bool             beginRequest(const std::string& permission);
        void             endRequest(const std::string& permission);
        size_t           pendingCount() const;

        SDisplayIdentity m_display;

      private:
        std::map<std::string, eGrantLifetime, std::less<>> m_grants;
        std::set<std::string, std::less<>>                 m_pending;
    };
}
