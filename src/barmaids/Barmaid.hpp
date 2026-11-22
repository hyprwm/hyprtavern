#pragma once

#include "../bus/Registry.hpp"

#include <common/EventLoop.hpp>
#include <common/Memory.hpp>

#include <hp_hyprtavern_barmaid_v1-client.hpp>

#include <hyprutils/signal/Signal.hpp>
#include <hyprwire/hyprwire.hpp>

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace Barmaids {
    // a trusted helper process started by the tavern. The tavern talks to it over a private control channel,
    // where the barmaid is the server, and optionally gives it a connection to the bus.
    class CBarmaid {
      public:
        CBarmaid(std::string binary, Bus::eProviderRole role, bool needsBus);
        virtual ~CBarmaid();

        bool launch(const SP<Hyprwire::IServerSocket>& bus, CEventLoop& loop);

        bool ready() const;

        // reaps the process if it exited
        void reap();

        // forwards an environment update the tavern already validated and authorized
        void updateEnvironment(const std::vector<const char*>& names, const std::vector<const char*>& values);

        struct {
            Hyprutils::Signal::CSignalT<> ready;
            Hyprutils::Signal::CSignalT<> died;
        } m_events;

      protected:
        // called once the control channel is connected, to bind the barmaid's service protocol
        virtual void onConnected() = 0;

        // runs done once the barmaid processed everything sent before this call. Only valid once ready.
        void                        sync(std::function<void()>&& done);

        SP<Hyprwire::IClientSocket> m_control;

      private:
        void                                     dispatch();
        void                                     onReady();
        void                                     die();
        void                                     terminate();

        std::string                              m_binary;
        Bus::eProviderRole                       m_role     = Bus::eProviderRole::NONE;
        bool                                     m_needsBus = false;

        pid_t                                    m_pid       = -1;
        int                                      m_controlFd = -1;
        CEventLoop*                              m_loop      = nullptr;
        bool                                     m_connected = false;
        bool                                     m_ready     = false;
        bool                                     m_dead      = false;

        SP<CCHpHyprtavernBarmaidManagerV1Object> m_manager;
        std::deque<std::function<void()>>        m_syncs;

        struct SEnvironmentUpdate {
            std::vector<std::string> names, values;
        };

        // updates sent before the manager was bound
        std::vector<SEnvironmentUpdate> m_pendingEnvironment;
    };
}
