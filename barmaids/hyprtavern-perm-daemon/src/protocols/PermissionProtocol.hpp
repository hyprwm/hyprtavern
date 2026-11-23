#pragma once

#include "../core/Transaction.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_permission_authentication_v1-server.hpp>

#include <vector>

class CTransactionResource {
  public:
    CTransactionResource(SP<CHpHyprtavernPermissionAuthenticationTransactionV1Object> resource);
    ~CTransactionResource();

    const Prompts::SRequest& request() const;

    // completes the transaction, at most once
    void                     sendResult(hpHyprtavernPermissionAuthenticationV1ResponseType response);

    WP<CTransactionResource> m_self;

  private:
    void                                                         fail(const Prompts::STransactionError& error);

    SP<CHpHyprtavernPermissionAuthenticationTransactionV1Object> m_resource;
    Prompts::CTransactionState                                   m_state;
    bool                                                         m_done = false;
};

class CPermissionProtocol {
  public:
    CPermissionProtocol(const SP<Hyprwire::IServerSocket>& socket);
    ~CPermissionProtocol();

    // whether prompts can be shown, sent to the tavern when it changes
    void setAvailable(bool available);
    bool available() const;

  private:
    void                                                                  bindManager(SP<Hyprwire::IObject> object);
    void                                                                  destroyResource(CTransactionResource* resource);

    std::vector<SP<CHpHyprtavernPermissionAuthenticationManagerV1Object>> m_managers;
    std::vector<SP<CTransactionResource>>                                 m_transactions;
    WP<Hyprwire::IServerSocket>                                           m_socket;
    bool                                                                  m_available = false;

    friend class CTransactionResource;
};

namespace PROTO {
    inline UP<CPermissionProtocol> permissions;
}
