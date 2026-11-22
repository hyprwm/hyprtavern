#pragma once

#include "../bus/PrincipalTokens.hpp"
#include "../bus/Registry.hpp"
#include "../security/PermissionContext.hpp"
#include "../security/Policy.hpp"

#include <common/Memory.hpp>

#include <hyprwire/hyprwire.hpp>

#include <string_view>
#include <vector>

namespace Core {
    // a single client connection to the tavern
    class CClient {
      public:
        CClient(WP<Hyprwire::IServerClient> wire, uint64_t id, Security::SPrincipal principal, Security::ePolicy policy, Bus::eProviderRole role);

        // unique for the lifetime of the tavern, never reused by a replacement connection
        uint64_t                    id() const;

        const Security::SPrincipal& principal() const;
        Security::ePolicy           policy() const;
        Bus::eProviderRole          role() const;
        SP<Hyprwire::IServerClient> wire() const;

        Security::eAuthorization    authorize(std::string_view permission) const;
        bool                        hasPermissions(const std::vector<std::string>& permissions) const;

        // whether the client may discover and inspect a bus object, not necessarily connect to it
        bool                    canInspect(const Bus::SObject& object) const;

        Bus::SPrincipalSnapshot snapshot() const;

        // explicit grants come from the client's live permission context, if any
        WP<Security::CPermissionContext> m_permissions;

      private:
        WP<Hyprwire::IServerClient> m_wire;
        uint64_t                    m_id = 0;
        Security::SPrincipal        m_principal;
        Security::ePolicy           m_policy = Security::ePolicy::ENFORCED;
        Bus::eProviderRole          m_role   = Bus::eProviderRole::NONE;
    };
}
