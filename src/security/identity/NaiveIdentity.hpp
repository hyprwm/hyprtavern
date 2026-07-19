#pragma once

#include "SecurityIdentity.hpp"

namespace Security {
    class CNaiveSecurityIdentityProvider : public ISecurityIdentityProvider {
      public:
        CNaiveSecurityIdentityProvider(SP<Hyprwire::IServerClient> client);
        virtual ~CNaiveSecurityIdentityProvider() = default;

        virtual eProviderType                     type() const override;
        virtual bool                              trustworthy() const override;

        virtual const std::optional<std::string>& appID() const override;
        virtual const std::optional<std::string>& displayName() const override;
        virtual const std::optional<std::string>& path() const override;

        virtual int                               pid() const override;

        virtual const std::string&                identity() const override;

      private:
        std::optional<std::string> m_appID;
        std::optional<std::string> m_displayName;
        std::optional<std::string> m_path;
        std::string                m_identity;

        int                        m_pid         = -1;
        bool                       m_trustworthy = false;
    };
}
