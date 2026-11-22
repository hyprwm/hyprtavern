#pragma once

#include "BusObject.hpp"
#include "Helpers.hpp"
#include "ObjectHandle.hpp"
#include "Permissions.hpp"
#include "Principal.hpp"

#include <hyprtavern-client/Tavern.hpp>

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-client.hpp>

#include <deque>
#include <vector>

namespace Hyprtavern {
    class CTavern : public ITavern {
      public:
        CTavern(SP<Hyprwire::IClientSocket> socket);
        virtual ~CTavern() = default;

        // handshakes, binds the core protocol and waits until the tavern is ready
        std::expected<void, eError>                     init(const STavernOptions& options, Clock::time_point deadline);

        int                                             fd() override;
        bool                                            dispatch() override;

        void                                            requestPermissions(const std::vector<std::string>& permissions, ePermissionMode mode, PermissionCallback&& done) override;
        std::expected<PermissionResults, eError>        requestPermissionsSync(const std::vector<std::string>& permissions, ePermissionMode mode,
                                                                               std::chrono::milliseconds timeout) override;

        void                                            find(const SQuery& query, FindCallback&& done) override;
        std::expected<std::vector<SObjectInfo>, eError> findSync(const SQuery& query, std::chrono::milliseconds timeout) override;

        void                                            connect(uint32_t objectID, const ClientImplementations& implementations, ConnectCallback&& done) override;
        std::expected<SP<Hyprwire::IClientSocket>, eError> connectSync(uint32_t objectID, const ClientImplementations& implementations, std::chrono::milliseconds timeout) override;
        std::expected<SP<Hyprwire::IClientSocket>, eError> connectSync(const SQuery& query, const ClientImplementations& implementations,
                                                                       std::chrono::milliseconds timeout) override;

        std::expected<SP<IBusObject>, eError>              createObject(const std::string& name, const std::vector<std::string>& requiredPermissions) override;

        void                                               updateEnvironment(const EnvironmentUpdate& environment, EnvironmentCallback&& done) override;
        std::expected<void, eError>                        updateEnvironmentSync(const EnvironmentUpdate& environment, std::chrono::milliseconds timeout) override;

        // the sync helpers, until a deadline
        std::expected<std::vector<SObjectInfo>, eError>    findUntil(const SQuery& query, Clock::time_point deadline);
        std::expected<SP<Hyprwire::IClientSocket>, eError> connectUntil(uint32_t objectID, const ClientImplementations& implementations, Clock::time_point deadline);
        std::expected<SP<Hyprwire::IClientSocket>, eError> connectUntil(const SQuery& query, const ClientImplementations& implementations, Clock::time_point deadline);

        // dispatches until done returns true
        std::expected<void, eError> waitUntil(const std::function<bool()>& done, Clock::time_point deadline);

        void                        redeem(const std::string& token, CPrincipalRedemption::DoneFn&& done);

        // destroys a bus object, but keeps it for events the tavern sent before it saw that
        void                                  retire(SP<CCHpHyprtavernBusObjectV1Object> object);

        WP<CTavern>                           m_self;
        SP<Hyprwire::IClientSocket>           m_socket;
        SP<CCHpHyprtavernCoreManagerV1Object> m_manager;

      private:
        struct SFind {
            SP<CCHpHyprtavernBusQueryV1Object> query;
            std::vector<SObjectInfo>           results;
            size_t                             pending = 0;
            FindCallback                       callback;
            bool                               done = false;

            void                               finish();
        };

        // runs an async request until it calls back, or the deadline passes
        template <typename T>
        std::expected<T, eError>                         await(Clock::time_point deadline, const std::function<void(std::function<void(std::expected<T, eError>)>&&)>& start);

        void                                             bindManager(SP<Hyprwire::IObject> object);
        void                                             onResults(SFind* request, const std::vector<uint32_t>& ids);
        void                                             sweep();

        bool                                             m_ready = false;
        std::vector<UP<SFind>>                           m_finds;
        std::vector<UP<CObjectHandle>>                   m_handles;
        std::vector<UP<CPrincipalRedemption>>            m_redemptions;
        std::vector<SP<CCHpHyprtavernBusObjectV1Object>> m_retiredObjects;
        std::deque<EnvironmentCallback>                  m_environmentResults;
        UP<CPermissions>                                 m_permissions;
    };
}
