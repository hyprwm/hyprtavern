#pragma once

#include "Helpers.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_kv_store_v1-client.hpp>

#include <hyprwire/hyprwire.hpp>

#include <expected>
#include <optional>
#include <string>

namespace Hyprtavern {
    class CTavern;

    // a minimal, blocking client for the app's own kv values. Drop it after any failure.
    class CKvClient {
      public:
        CKvClient(SP<Hyprwire::IClientSocket> socket);

        // connects through the tavern, and waits until the store is available
        static UP<CKvClient> connect(CTavern& tavern, Clock::time_point deadline);

        // nullopt if the value is missing, which is not a failure
        std::expected<std::optional<std::string>, eError> get(const std::string& key, Clock::time_point deadline);
        std::expected<void, eError>                       set(const std::string& key, const std::string& value, Clock::time_point deadline);

      private:
        void                                     bindManager(SP<Hyprwire::IObject> object);
        std::expected<void, eError>              waitUntil(const std::function<bool()>& done, Clock::time_point deadline);

        SP<Hyprwire::IClientSocket>              m_socket;
        SP<CCHpHyprtavernKvStoreManagerV1Object> m_manager;
        bool                                     m_available = false;

        // the outcome of the single pending request: a value, or why it failed
        std::optional<std::expected<std::string, uint32_t>> m_result;
    };
}
