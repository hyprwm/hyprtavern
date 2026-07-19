#pragma once

#include "../../helpers/Memory.hpp"

#include <string>
#include <cstdint>
#include <optional>

#include <hyprwire/hyprwire.hpp>

namespace Security {
    enum eProviderType : uint8_t {
        PROVIDER_NAIVE = 0,
    };

    class ISecurityIdentityProvider {
      public:
        virtual ~ISecurityIdentityProvider() = default;

        virtual eProviderType type() const        = 0;
        virtual bool          trustworthy() const = 0;

        // Display stuff
        virtual const std::optional<std::string>& appID() const       = 0;
        virtual const std::optional<std::string>& displayName() const = 0;
        virtual const std::optional<std::string>& path() const        = 0;

        // data
        virtual int pid() const = 0;

        // The only thing that matters. This is a descriptive string
        // of the identity, could be an app identifier, pid+gid+uid tuple, etc.
        virtual const std::string& identity() const = 0;

      protected:
        ISecurityIdentityProvider() = default;
    };

    UP<ISecurityIdentityProvider> identify(SP<Hyprwire::IServerClient> client);
}
