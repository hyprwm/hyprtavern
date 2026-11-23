#pragma once

#include <hyprtavern-client/BusObject.hpp>

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-client.hpp>

#include <hyprutils/os/FileDescriptor.hpp>

#include <unordered_map>
#include <unordered_set>

namespace Hyprtavern {
    class CTavern;

    class CBusObject : public IBusObject {
      public:
        CBusObject(SP<CTavern> tavern, SP<CCHpHyprtavernBusObjectV1Object> object, SP<Hyprwire::IServerSocket> server);
        virtual ~CBusObject();

        void                              expose(const SExposeOptions& options) override;
        std::expected<void, eExposeError> exposeSync(const SExposeOptions& options) override;

        bool                              setProperty(const std::string& name, const std::string& value) override;
        bool                              requirePermissions(const std::vector<std::string>& permissions) override;

        int                               fd() override;
        bool                              dispatch() override;

        const SPrincipal*                 principal(const SP<Hyprwire::IServerClient>& client) const override;

        WP<CBusObject>                    m_self;

        // the bus couldn't host it, so it's inert
        bool m_failed = false;

      private:
        struct SClient {
            WP<Hyprwire::IServerClient> client;
            SPrincipal                  principal;
        };

        void                                                  onNewFd(int fd, const char* token, const std::vector<const char*>& scope);
        void                                                  addClient(Hyprutils::OS::CFileDescriptor fd, SPrincipal principal);

        SP<CTavern>                                           m_tavern;
        SP<CCHpHyprtavernBusObjectV1Object>                   m_object;
        SP<Hyprwire::IServerSocket>                           m_server;
        std::unordered_set<std::string>                       m_exposed;
        std::unordered_map<Hyprwire::IServerClient*, SClient> m_clients;
    };
}
