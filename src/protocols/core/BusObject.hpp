#pragma once

#include "../../bus/Registry.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

#include <string>
#include <vector>

// a registration on the bus, removed with the resource
class CBusObjectResource {
  public:
    CBusObjectResource(SP<CHpHyprtavernBusObjectV1Object> resource, uint32_t id, Bus::eProviderRole role);
    ~CBusObjectResource();

    uint32_t id() const;

    // hands the provider its end of an admitted connection
    void sendNewFd(int fd, const std::string& token, const std::vector<std::string>& scope);

  private:
    SP<CHpHyprtavernBusObjectV1Object> m_resource;
    uint32_t                           m_id   = 0;
    Bus::eProviderRole                 m_role = Bus::eProviderRole::NONE;
};
