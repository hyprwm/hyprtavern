#pragma once

#include "../../barmaids/KvBarmaid.hpp"
#include "../../barmaids/PermissionBarmaid.hpp"
#include "../../core/Client.hpp"
#include "../../security/PermissionContext.hpp"
#include "../../security/SavedContext.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

#include <hyprutils/signal/Listener.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

// grants for a single client connection. Permanent ones are saved for the application, and shared by all its contexts.
class CPermissionContextResource {
  public:
    CPermissionContextResource(SP<CHpHyprtavernPermissionContextV1Object> resource, SP<Core::CClient> client);
    ~CPermissionContextResource();

    // restores the application's saved context, if it has one
    void                           begin();

    uint64_t                       clientID() const;

    WP<CPermissionContextResource> m_self;

  private:
    enum class eState : uint8_t {
        SESSION,     // no saved context can be used, session grants and policy only
        RESTORING,   // waiting on the store, requests are deferred
        PERSISTENT,  // the application's saved context is restored, or there is none yet
        UNAVAILABLE, // inert
    };

    void onSetDisplayIdentity(const char* name, const char* description);
    void onRequestPermission(const char* permission, uint32_t mode);

    void restore();
    void onRestored(std::expected<std::string, Barmaids::CKvBarmaid::eError> value);
    void adopt(const Security::SSavedContext& saved);
    void settle(eState state);
    void makeUnavailable();

    void process(const std::string& permission, bool permanent);
    void onPromptResult(const std::string& permission, Barmaids::CPermissionBarmaid::eResult result);
    void grantForSession(const std::string& permission);
    void persistGrant(const std::string& permission);
    void finish(const std::string& permission, uint32_t result, Security::eGrantLifetime lifetime = Security::eGrantLifetime::SESSION);

    // writes the application's saved context, with an extra permanent grant if given. stored is only true if that grant made it in.
    void                                       save(std::optional<std::string> extraGrant, std::function<void(bool stored)>&& done);

    SP<CHpHyprtavernPermissionContextV1Object> m_resource;
    WP<Core::CClient>                          m_client;
    uint64_t                                   m_clientID = 0;
    SP<Security::CPermissionContext>           m_context;
    eState                                     m_state = eState::SESSION;

    // the owner of the application's saved context, and where it's stored
    Security::SAppIdentity                                           m_owner;
    std::string                                                      m_key;

    std::vector<std::pair<std::string, bool>>                        m_deferred;
    std::map<std::string, UP<Barmaids::CPermissionBarmaid::CPrompt>> m_prompts;
    Hyprutils::Signal::CHyprSignalListener                           m_storeListener;
};
