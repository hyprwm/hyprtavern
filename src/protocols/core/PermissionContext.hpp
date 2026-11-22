#pragma once

#include "../../barmaids/KvBarmaid.hpp"
#include "../../barmaids/PermissionBarmaid.hpp"
#include "../../core/Client.hpp"
#include "../../security/PermissionContext.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

#include <hyprutils/signal/Listener.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

// grants for a single client connection, optionally persisted under a token
class CPermissionContextResource {
  public:
    CPermissionContextResource(SP<CHpHyprtavernPermissionContextV1Object> resource, SP<Core::CClient> client);
    ~CPermissionContextResource();

    // restores the saved context selected by token, or creates a new one when possible
    void                           begin(const std::string& token);

    uint64_t                       clientID() const;

    WP<CPermissionContextResource> m_self;

  private:
    enum class eState : uint8_t {
        SESSION,     // no verified saved context, session grants and policy only
        RESTORING,   // waiting on the store, requests are deferred
        PERSISTENT,  // backed by a verified saved context
        UNAVAILABLE, // inert
    };

    void onSetDisplayIdentity(const char* name, const char* description);
    void onRequestPermission(const char* permission, uint32_t mode);

    void restore();
    void onRestored(std::expected<std::string, Barmaids::CKvBarmaid::eError> value);
    void create();
    void settle(eState state);
    void makeUnavailable();

    void process(const std::string& permission, bool permanent);
    void onPromptResult(const std::string& permission, Barmaids::CPermissionBarmaid::eResult result);
    void grantForSession(const std::string& permission);
    void persistGrant(const std::string& permission);
    void finish(const std::string& permission, uint32_t result, Security::eGrantLifetime lifetime = Security::eGrantLifetime::SESSION);

    // writes the saved context, with an extra permanent grant if given
    void                                       save(const std::string& token, std::optional<std::string> extraGrant, std::function<void(bool stored)>&& done);

    SP<CHpHyprtavernPermissionContextV1Object> m_resource;
    WP<Core::CClient>                          m_client;
    SP<Security::CPermissionContext>           m_context;
    eState                                     m_state = eState::SESSION;

    // the durable token, empty without a verified saved context
    std::string m_token;

    // the supplied token, an unverified restoration hint
    std::string                                                      m_hint;

    std::vector<std::pair<std::string, bool>>                        m_deferred;
    std::map<std::string, UP<Barmaids::CPermissionBarmaid::CPrompt>> m_prompts;
    Hyprutils::Signal::CHyprSignalListener                           m_storeListener;
};
