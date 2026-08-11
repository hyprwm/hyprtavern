#pragma once

#include "../../helpers/Memory.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <hyprwire/hyprwire.hpp>

namespace Security {
    enum eProviderType : uint8_t {
        PROVIDER_NAIVE = 0,
    };

    enum class eIdentityClass : uint8_t {
        INTERNAL,
        HOST_SAME_UID,
        SANDBOXED,
        UNKNOWN,
    };

    enum class ePrincipalAuthority : uint8_t {
        INTERNAL,
        EXTERNAL,
    };

    class ISecurityIdentityProvider {
      public:
        virtual ~ISecurityIdentityProvider() = default;

        virtual eProviderType  type() const           = 0;
        virtual eIdentityClass classification() const = 0;
        virtual bool           trustworthy() const    = 0;

        // Display and application identity data.
        virtual const std::optional<std::string>& appID() const           = 0;
        virtual const std::optional<std::string>& displayName() const     = 0;
        virtual const std::optional<std::string>& path() const            = 0;
        virtual bool                              appIDPersistent() const = 0;

        virtual int                               pid() const = 0;

        // Stable security identity used to bind persistent permission tokens.
        virtual const std::string& identity() const = 0;

      protected:
        ISecurityIdentityProvider() = default;
    };

    UP<ISecurityIdentityProvider> identify(SP<Hyprwire::IServerClient> client, ePrincipalAuthority authority = ePrincipalAuthority::EXTERNAL);
}
