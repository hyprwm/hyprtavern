#include "PermissionContext.hpp"
#include "Arguments.hpp"
#include "CoreProtocol.hpp"

#include <common/Limits.hpp>
#include <common/Log.hpp>
#include <common/PermissionName.hpp>

#include <algorithm>
#include <format>
#include <unordered_map>

using namespace Security;

namespace {
    // the latest stored saved context of each application, by key, so the writes of all its contexts build on each other
    std::unordered_map<std::string, SSavedContext> g_savedContexts;
}

CPermissionContextResource::CPermissionContextResource(SP<CHpHyprtavernPermissionContextV1Object> resource, SP<Core::CClient> client) :
    m_resource(std::move(resource)), m_client(client), m_clientID(client->id()), m_context(makeShared<CPermissionContext>()) {
    m_resource->setDestroy([this] { PROTO::core->destroyResource(this); });
    m_resource->setOnDestroy([this] { PROTO::core->destroyResource(this); });

    m_resource->setSetDisplayIdentity([this](const char* name, const char* description) { onSetDisplayIdentity(name, description); });
    m_resource->setRequestPermission([this](const char* permission, hpHyprtavernCoreV1SecurityPermissionMode mode) { onRequestPermission(permission, mode); });

    client->m_permissions = m_context;
}

CPermissionContextResource::~CPermissionContextResource() {
    m_resource->setOnDestroy({});
}

uint64_t CPermissionContextResource::clientID() const {
    return m_clientID;
}

void CPermissionContextResource::begin() {
    m_owner = m_client->principal().identity;

    // only stable identities may own saved contexts
    if (!m_owner.persistable())
        return;

    const auto KEY = savedContextKey(m_owner);
    if (!KEY) {
        g_logger->log(LOG_WARN, "the identity of client {} is too long to save its permissions", m_clientID);
        return;
    }

    m_key = *KEY;

    if (Barmaids::kv->available()) {
        restore();
        return;
    }

    // session grants only, until the store is up
    m_storeListener = Barmaids::kv->m_storeEvents.available.listen([this] {
        m_storeListener.reset();
        restore();
    });
}

void CPermissionContextResource::restore() {
    // another context of the application may have stored it already
    if (const auto SAVED = g_savedContexts.find(m_key); SAVED != g_savedContexts.end()) {
        adopt(SAVED->second);
        return;
    }

    m_state = eState::RESTORING;

    Barmaids::kv->get(m_key, [self = m_self](std::expected<std::string, Barmaids::CKvBarmaid::eError> value) {
        if (const auto SELF = self.lock())
            SELF->onRestored(std::move(value));
    });
}

void CPermissionContextResource::onRestored(std::expected<std::string, Barmaids::CKvBarmaid::eError> value) {
    if (!value) {
        // nothing saved yet, it's created with the first permanent grant
        if (value.error() == Barmaids::CKvBarmaid::eError::MISSING) {
            settle(eState::PERSISTENT);
            return;
        }

        // a failed lookup is not a missing value, so writing could replace saved grants
        settle(eState::SESSION);
        return;
    }

    // a newer one, if another context of the application stored it in the meantime
    auto saved = g_savedContexts.find(m_key);

    if (saved == g_savedContexts.end()) {
        const auto PARSED = parseSavedContext(*value);

        // never overwritten, so nothing is lost
        if (!PARSED || PARSED->owner != m_owner) {
            g_logger->log(LOG_ERR, "the saved permissions of client {} are malformed", m_clientID);
            makeUnavailable();
            return;
        }

        saved = g_savedContexts.emplace(m_key, *PARSED).first;
    }

    adopt(saved->second);
}

void CPermissionContextResource::adopt(const SSavedContext& saved) {
    // all or nothing
    if (!m_context->restore(saved.grants)) {
        makeUnavailable();
        return;
    }

    // a display identity set during this session wins
    if (m_context->m_display == SDisplayIdentity{})
        m_context->m_display = saved.display;

    settle(eState::PERSISTENT);
}

void CPermissionContextResource::settle(eState state) {
    m_state = state;

    for (const auto& [permission, permanent] : std::exchange(m_deferred, {})) {
        process(permission, permanent);
    }
}

void CPermissionContextResource::makeUnavailable() {
    m_state = eState::UNAVAILABLE;

    // no grants remain active, and pending requests receive no results
    m_context.reset();
    m_prompts.clear();
    m_deferred.clear();

    m_resource->sendUnavailable();
}

void CPermissionContextResource::save(std::optional<std::string> extraGrant, std::function<void(bool stored)>&& done) {
    auto record = makeShared<SSavedContext>();

    // produced when sent, so it builds on every write before it, from any context of the application
    const auto PRODUCE = [self = m_self, record, extraGrant]() -> std::optional<std::string> {
        const auto SELF = self.lock();
        if (!SELF || !SELF->m_context)
            return std::nullopt;

        const auto SAVED = g_savedContexts.find(SELF->m_key);
        *record          = SAVED == g_savedContexts.end() ? SSavedContext{.owner = SELF->m_owner} : SAVED->second;

        // an empty display identity never replaces a saved one
        if (SELF->m_context->m_display != SDisplayIdentity{})
            record->display = SELF->m_context->m_display;

        // a full one takes no more grants, so it can always be restored
        if (extraGrant && !std::ranges::contains(record->grants, *extraGrant) && record->grants.size() < MAX_GRANTS)
            record->grants.emplace_back(*extraGrant);

        return serializeSavedContext(*record);
    };

    Barmaids::kv->set(m_key, PRODUCE, [self = m_self, key = m_key, record, extraGrant, done = std::move(done)](bool stored) {
        if (stored)
            g_savedContexts[key] = *record;

        if (self)
            done(stored && (!extraGrant || std::ranges::contains(record->grants, *extraGrant)));
    });
}

void CPermissionContextResource::onSetDisplayIdentity(const char* name, const char* description) {
    const SDisplayIdentity DISPLAY = {.name = name, .description = description};

    if (DISPLAY.name.size() > MAX_DISPLAY_NAME_LENGTH || DISPLAY.description.size() > MAX_DISPLAY_DESCRIPTION_LENGTH) {
        m_resource->error(HP_HYPRTAVERN_CORE_V1_PROTOCOL_ERRORS_INVALID_ARGUMENT, "display identity is too long");
        return;
    }

    if (m_state == eState::UNAVAILABLE)
        return;

    m_context->m_display = DISPLAY;

    // saved contexts are only created with a permanent grant
    if (m_state == eState::PERSISTENT && g_savedContexts.contains(m_key))
        save(std::nullopt, [](bool) {});
}

void CPermissionContextResource::onRequestPermission(const char* permission, uint32_t mode) {
    if (mode > HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT) {
        m_resource->error(HP_HYPRTAVERN_CORE_V1_PROTOCOL_ERRORS_INVALID_ARGUMENT, "invalid permission mode");
        return;
    }

    if (!isRequestablePermission(permission)) {
        m_resource->error(HP_HYPRTAVERN_CORE_V1_PROTOCOL_ERRORS_INVALID_PERMISSION, "invalid permission request");
        return;
    }

    // inert, no results are sent anymore
    if (m_state == eState::UNAVAILABLE)
        return;

    const std::string PERMISSION = permission;

    if (!m_context->beginRequest(PERMISSION)) {
        m_resource->error(HP_HYPRTAVERN_CORE_V1_PROTOCOL_ERRORS_INVALID_STATE, std::format("a request for {} is already pending", PERMISSION));
        return;
    }

    // bounded, so a single context can't queue prompts without end
    if (m_context->pendingCount() > MAX_PENDING_REQUESTS) {
        finish(PERMISSION, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
        return;
    }

    const bool PERMANENT = mode == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT;

    // restored grants could already satisfy it
    if (m_state == eState::RESTORING) {
        m_deferred.emplace_back(PERMISSION, PERMANENT);
        return;
    }

    process(PERMISSION, PERMANENT);
}

void CPermissionContextResource::process(const std::string& permission, bool permanent) {
    // from a newer catalog, so it can't be granted here
    if (!isKnownPermission(permission)) {
        finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
        return;
    }

    // derived from host status, never prompted for or stored
    if (permission == PERMISSION_UNSANDBOXED) {
        const bool HOST = m_client->authorize(permission) != eAuthorization::DENIED;
        finish(permission, HOST ? HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED_BY_POLICY : HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_DENIED);
        return;
    }

    if (const auto LIFETIME = m_context->grantFor(permission)) {
        finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_ALREADY_GRANTED, *LIFETIME);
        return;
    }

    if (m_client->policy() == ePolicy::BYPASS_ORDINARY) {
        finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED_BY_POLICY);
        return;
    }

    if (m_context->grantCount() >= MAX_GRANTS) {
        finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
        return;
    }

    // a session request never creates a permanent grant
    const bool PERSIST = permanent && m_state == eState::PERSISTENT;

    auto       prompt = Barmaids::permissions->ask(m_client->principal().identity, m_context->m_display.name, permission, PERSIST,
                                                   [this, permission](Barmaids::CPermissionBarmaid::eResult result) { onPromptResult(permission, result); });

    if (!prompt) {
        finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
        return;
    }

    m_prompts[permission] = std::move(prompt);
}

void CPermissionContextResource::onPromptResult(const std::string& permission, Barmaids::CPermissionBarmaid::eResult result) {
    // the prompt is done, dropping it does not dismiss anything
    const auto PROMPT = std::move(m_prompts[permission]);
    m_prompts.erase(permission);

    switch (result) {
        case Barmaids::CPermissionBarmaid::eResult::ACCEPTED_SESSION: grantForSession(permission); break;
        case Barmaids::CPermissionBarmaid::eResult::ACCEPTED_PERSISTENT: persistGrant(permission); break;
        case Barmaids::CPermissionBarmaid::eResult::DENIED: finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_DENIED); break;
        case Barmaids::CPermissionBarmaid::eResult::FAILED: finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED); break;
    }
}

void CPermissionContextResource::grantForSession(const std::string& permission) {
    if (!m_context->addGrant(permission, eGrantLifetime::SESSION)) {
        finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
        return;
    }

    finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED);
}

void CPermissionContextResource::persistGrant(const std::string& permission) {
    // consent is not durability: only report a permanent grant once it is stored
    save(permission, [this, permission](bool stored) {
        if (!stored) {
            g_logger->log(LOG_WARN, "failed to persist {}, granting it for the session", permission);
            grantForSession(permission);
            return;
        }

        if (!m_context->addGrant(permission, eGrantLifetime::PERMANENT)) {
            finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_FAILED);
            return;
        }

        finish(permission, HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED, eGrantLifetime::PERMANENT);
    });
}

void CPermissionContextResource::finish(const std::string& permission, uint32_t result, eGrantLifetime lifetime) {
    m_context->endRequest(permission);

    const auto MODE = lifetime == eGrantLifetime::PERMANENT ? HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT : HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_SESSION;
    m_resource->sendPermissionResult(permission.c_str(), result, MODE);
}
