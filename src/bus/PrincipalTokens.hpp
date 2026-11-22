#pragma once

#include "../security/PermissionContext.hpp"
#include "../security/Policy.hpp"

#include <common/Memory.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bus {
    constexpr auto   PRINCIPAL_TOKEN_TTL  = std::chrono::seconds{30};
    constexpr size_t MAX_PRINCIPAL_TOKENS = 4096;

    // admission-time state of a connecting principal, independent of its client staying alive
    struct SPrincipalSnapshot {
        Security::SPrincipal       principal;
        Security::ePolicy          policy = Security::ePolicy::ENFORCED;
        Security::SDisplayIdentity display;
        std::vector<std::string>   grants;
    };

    struct SRedeemedPrincipal {
        SPrincipalSnapshot snapshot;

        // client connection the principal was admitted for
        uint64_t source = 0;
    };

    // short-lived, single-use tokens attached to routed connections
    class CPrincipalTokens {
      public:
        using Clock = std::chrono::steady_clock;

        // empty when too many tokens are outstanding.
        std::string issue(SPrincipalSnapshot snapshot, uint64_t source, uint64_t recipient, Clock::time_point now = Clock::now());

        // only the intended recipient may redeem a token. Other attempts do not consume it.
        std::optional<SRedeemedPrincipal> redeem(const std::string& token, uint64_t recipient, Clock::time_point now = Clock::now());

      private:
        void prune(Clock::time_point now);

        struct SEntry {
            SRedeemedPrincipal principal;
            uint64_t           recipient = 0;
            Clock::time_point  expiry;
        };

        std::unordered_map<std::string, SEntry> m_tokens;
    };

    inline UP<CPrincipalTokens> tokens;
}
