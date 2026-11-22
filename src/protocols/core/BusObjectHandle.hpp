#pragma once

#include "../../core/Client.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

// bound to a single registration, never retargeted to a replacement provider
class CBusObjectHandleResource {
  public:
    CBusObjectHandleResource(SP<CHpHyprtavernBusObjectHandleV1Object> resource, SP<Core::CClient> client, uint32_t id);
    ~CBusObjectHandleResource();

  private:
    void onConnect(const std::vector<const char*>& protocols);

    // returns the error to fail the connection with, if any
    std::optional<hpHyprtavernCoreV1BusObjectConnectionError> admit(const std::vector<std::string>& protocols);

    enum class eState : uint8_t {
        INERT,
        READY,
        CONNECTED,
    };

    SP<CHpHyprtavernBusObjectHandleV1Object> m_resource;
    WP<Core::CClient>                        m_client;
    uint32_t                                 m_id    = 0;
    eState                                   m_state = eState::INERT;
};
