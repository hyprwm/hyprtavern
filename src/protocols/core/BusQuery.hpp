#pragma once

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

#include <vector>

// sends a single snapshot of matching object ids, then waits to be destroyed
class CBusQueryResource {
  public:
    CBusQueryResource(SP<CHpHyprtavernBusQueryV1Object> resource, const std::vector<uint32_t>& matches);
    ~CBusQueryResource();

  private:
    SP<CHpHyprtavernBusQueryV1Object> m_resource;
};
