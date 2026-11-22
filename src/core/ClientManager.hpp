#pragma once

#include "Client.hpp"

#include <optional>
#include <vector>

namespace Core {
    class CClientManager {
      public:
        CClientManager(bool enforcePermissions);

        // registers a connection, attesting its identity. Barmaids are registered with their role and an
        // internal identity before their first dispatch. Registering a known connection returns it.
        SP<CClient> add(const SP<Hyprwire::IServerClient>& wire, Bus::eProviderRole role = Bus::eProviderRole::NONE,
                        std::optional<Security::SAppIdentity> internalIdentity = std::nullopt);

        SP<CClient> get(const SP<Hyprwire::IServerClient>& wire) const;
        SP<CClient> get(uint64_t id) const;

      private:
        std::vector<SP<CClient>> m_clients;
        uint64_t                 m_nextID             = 1;
        bool                     m_enforcePermissions = false;
    };

    inline UP<CClientManager> clients;
}
