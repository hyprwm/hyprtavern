#pragma once

#include "../../core/Client.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

class CCoreManagerResource {
  public:
    CCoreManagerResource(SP<CHpHyprtavernCoreManagerV1Object> resource, SP<Core::CClient> client);
    ~CCoreManagerResource();

    // sent at most once. Other requests before ready are protocol errors.
    void sendReady();

  private:
    bool ensureReady();

    void onCreatePermissionContext(uint32_t seq, const char* token);
    void onRedeemPrincipalToken(uint32_t seq, const char* token);
    void onGetBusObject(uint32_t seq, const char* name, const std::vector<const char*>& requirements);
    void onGetQueryObject(uint32_t seq, const std::vector<const char*>& protocols, uint32_t protocolMode, const std::vector<const char*>& properties, uint32_t propertyMode);
    void onGetObjectHandle(uint32_t seq, uint32_t id);
    void onUpdateEnvironment(const std::vector<const char*>& names, const std::vector<const char*>& values);

    SP<CHpHyprtavernCoreManagerV1Object> m_resource;
    WP<Core::CClient>                    m_client;
    bool                                 m_ready = false;
};
