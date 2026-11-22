#pragma once

#include "Barmaid.hpp"

#include <hp_hyprtavern_kv_store_v1-client.hpp>

#include <deque>
#include <expected>
#include <map>
#include <optional>

namespace Barmaids {
    // the key-value store barmaid. The tavern only uses tavern-scoped values over the control channel.
    class CKvBarmaid : public CBarmaid {
      public:
        CKvBarmaid();

        enum class eError : uint8_t {
            MISSING,
            FAILED,
        };

        using GetCallback = std::function<void(std::expected<std::string, eError>)>;
        using SetCallback = std::function<void(bool stored)>;

        // produces a value when its write is sent, so it can build on the results of earlier writes.
        // returning nullopt cancels the write without running its callback.
        using ValueFn = std::function<std::optional<std::string>()>;

        bool available() const;

        // requests are queued until the store is available, and serialized per key
        void get(const std::string& key, GetCallback&& callback);

        // an empty value deletes the key. stored is true once the write is durable.
        void set(const std::string& key, ValueFn&& value, SetCallback&& callback);

        struct {
            Hyprutils::Signal::CSignalT<> available;
        } m_storeEvents;

      protected:
        virtual void onConnected();

      private:
        struct SRequest {
            std::optional<ValueFn> value;
            GetCallback            onGet;
            SetCallback            onSet;
        };

        struct SQueue {
            std::deque<SRequest> requests;
            bool                 inFlight = false;
        };

        void                                     onAvailable();
        void                                     pump(const std::string& key);
        std::optional<SRequest>                  finish(const std::string& key, uint32_t type);

        SP<CCHpHyprtavernKvStoreManagerV1Object> m_manager;
        std::map<std::string, SQueue>            m_queues;
        bool                                     m_available = false;
    };

    inline UP<CKvBarmaid> kv;
}
