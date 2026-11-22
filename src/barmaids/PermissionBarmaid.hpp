#pragma once

#include "Barmaid.hpp"
#include "../security/Identity.hpp"

#include <hp_hyprtavern_permission_authentication_v1-client.hpp>

namespace Barmaids {
    // the barmaid showing permission prompts to the user
    class CPermissionBarmaid : public CBarmaid {
      public:
        CPermissionBarmaid();

        using CTransaction = CCHpHyprtavernPermissionAuthenticationTransactionV1Object;

        enum class eResult : uint8_t {
            ACCEPTED_SESSION,
            ACCEPTED_PERSISTENT,
            DENIED,
            FAILED,
        };

        // an outstanding prompt. Dropping it dismisses the prompt, after which its callback never runs.
        class CPrompt {
          public:
            CPrompt(SP<CTransaction> transaction);
            ~CPrompt();

          private:
            SP<CTransaction> m_transaction;
            bool             m_done = false;

            friend class CPermissionBarmaid;
        };

        // whether prompts can be shown, e.g. depending on the display environment
        bool available() const;

        // asks the user for a permission. persist offers a persistent grant in addition to a session one.
        // null if prompts are unavailable.
        UP<CPrompt> ask(const Security::SAppIdentity& identity, const std::string& displayName, const std::string& permission, bool persist,
                        std::function<void(eResult)>&& callback);

      protected:
        virtual void onConnected();

      private:
        void                                                      dismiss(SP<CTransaction> transaction);

        SP<CCHpHyprtavernPermissionAuthenticationManagerV1Object> m_manager;
        bool                                                      m_available = false;
    };

    inline UP<CPermissionBarmaid> permissions;
}
