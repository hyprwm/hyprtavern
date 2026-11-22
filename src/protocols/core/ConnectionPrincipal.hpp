#pragma once

#include "../../bus/PrincipalTokens.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

#include <optional>

// the principal admitted for a routed connection, as seen by its provider
class CConnectionPrincipalResource {
  public:
    // a missing principal means the token could not be redeemed
    CConnectionPrincipalResource(SP<CHpHyprtavernConnectionPrincipalV1Object> resource, std::optional<Bus::SRedeemedPrincipal> principal);
    ~CConnectionPrincipalResource();

  private:
    void                                         onRequery();
    void                                         sendSnapshot(const Bus::SPrincipalSnapshot& snapshot);
    void                                         fail();

    SP<CHpHyprtavernConnectionPrincipalV1Object> m_resource;

    // the admitted principal, empty once inert
    std::optional<Bus::SRedeemedPrincipal> m_principal;
};
