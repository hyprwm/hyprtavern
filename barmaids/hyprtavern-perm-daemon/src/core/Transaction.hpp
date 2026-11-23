#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace Prompts {
    // values match application_identity_kind in hp_hyprtavern_permission_authentication_v1
    enum class eIdentityKind : uint32_t {
        UNKNOWN          = 0,
        HOST_EXECUTABLE  = 1,
        FLATPAK          = 2,
        INTERNAL         = 3,
        HOST_APPLICATION = 4,
    };

    // values match protocol_errors in hp_hyprtavern_permission_authentication_v1
    enum class eTransactionError : uint32_t {
        INVALID_ARGUMENT   = 0,
        INVALID_STATE      = 1,
        INVALID_PERMISSION = 2,
    };

    struct STransactionError {
        eTransactionError code = eTransactionError::INVALID_ARGUMENT;
        std::string       message;
    };

    // a frozen permission request
    struct SRequest {
        std::string   displayName;
        std::string   attestedID;
        eIdentityKind kind = eIdentityKind::UNKNOWN;
        std::string   permission;
        bool          persist = false;
    };

    // validates a transaction's requests in protocol order. Errors are protocol errors.
    class CTransactionState {
      public:
        std::expected<void, STransactionError> setDisplayName(std::string name);
        std::expected<void, STransactionError> setAttestedID(std::string id, uint32_t kind);

        // freezes the request
        std::expected<void, STransactionError> ask(std::string permission, uint32_t askType);

        bool                                   asked() const;
        const SRequest&                        request() const;

      private:
        SRequest m_request;
        bool     m_hasIdentity = false;
        bool     m_asked       = false;
    };
}
