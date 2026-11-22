#include "PermissionContext.hpp"
#include "Arguments.hpp"
#include "CoreProtocol.hpp"
#include "../../helpers/Token.hpp"
#include "../../security/SavedContext.hpp"

#include <common/Log.hpp>
#include <common/PermissionName.hpp>

#include <algorithm>
#include <format>

using namespace Security;

namespace {
    constexpr size_t MAX_DISPLAY_NAME_LENGTH        = 256;
    constexpr size_t MAX_DISPLAY_DESCRIPTION_LENGTH = 2048;

    std::string      contextKey(const std::string& token) {
        return std::format("context:{}", token);
    }
}

CPermissionContextResource::CPermissionContextResource(SP<CHpHyprtavernPermissionContextV1Object> resource, SP<Core::CClient> client) :
    m_resource(std::move(resource)), m_client(client), m_context(makeShared<CPermissionContext>()) {
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
    return m_client ? m_client->id() : 0;
}

void CPermissionContextResource::begin(const std::string& token) {
    // only stable identities may own saved contexts, retrying cannot help others
    if (!m_client->principal().identity.persistable()) {
        m_resource->sendPersistenceToken("");
        return;
    }

    m_hint = token;

    if (Barmaids::kv->available()) {
        restore();
        return;
    }

    // keep the hint, and retry once the store is up
    m_resource->sendPersistenceToken("");
    m_storeListener = Barmaids::kv->m_storeEvents.available.listen([this] {
        m_storeListener.reset();
        restore();
    });
}

void CPermissionContextResource::restore() {
    m_state = eState::RESTORING;

    // tokens we never issued cannot select a saved context
    if (!Token::isWellFormed(m_hint)) {
        create();
        return;
    }

    Barmaids::kv->get(contextKey(m_hint), [self = m_self](std::expected<std::string, Barmaids::CKvBarmaid::eError> value) {
        if (const auto SELF = self.lock())
            SELF->onRestored(std::move(value));
    });
}

void CPermissionContextResource::onRestored(std::expected<std::string, Barmaids::CKvBarmaid::eError> value) {
    if (!value) {
        // unknown tokens are never adopted, a fresh one is generated instead
        if (value.error() == Barmaids::CKvBarmaid::eError::MISSING) {
            create();
            return;
        }

        // a failed lookup is not a missing value, so the client must keep its token
        settle(eState::SESSION);
        m_resource->sendPersistenceToken("");
        return;
    }

    const auto SAVED = parseSavedContext(*value);

    if (!SAVED || SAVED->owner != m_client->principal().identity) {
        g_logger->log(LOG_WARN, "client {} tried to restore a permission context it does not own", m_client->id());
        makeUnavailable();
        return;
    }

    // all or nothing
    if (!m_context->restore(SAVED->grants)) {
        makeUnavailable();
        return;
    }

    // a display identity set during this session wins
    if (m_context->m_display == SDisplayIdentity{})
        m_context->m_display = SAVED->display;

    m_token = m_hint;
    settle(eState::PERSISTENT);
    m_resource->sendPersistenceToken(m_token.c_str());
}

void CPermissionContextResource::create() {
    const auto TOKEN = Token::generate();

    save(TOKEN, std::nullopt, [this, TOKEN](bool stored) {
        if (!stored) {
            settle(eState::SESSION);
            m_resource->sendPersistenceToken("");
            return;
        }

        m_token = TOKEN;
        settle(eState::PERSISTENT);
        m_resource->sendPersistenceToken(m_token.c_str());
    });
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

void CPermissionContextResource::save(const std::string& token, std::optional<std::string> extraGrant, std::function<void(bool stored)>&& done) {
    // produced when sent, so it includes every grant persisted before it
    const auto PRODUCE = [self = m_self, extraGrant]() -> std::optional<std::string> {
        const auto SELF = self.lock();
        if (!SELF || !SELF->m_context)
            return std::nullopt;

        SSavedContext saved = {
            .owner   = SELF->m_client->principal().identity,
            .display = SELF->m_context->m_display,
            .grants  = SELF->m_context->permanentGrants(),
        };

        if (extraGrant && !std::ranges::contains(saved.grants, *extraGrant))
            saved.grants.emplace_back(*extraGrant);

        return serializeSavedContext(saved);
    };

    Barmaids::kv->set(contextKey(token), PRODUCE, [self = m_self, done = std::move(done)](bool stored) {
        if (self)
            done(stored);
    });
}

void CPermissionContextResource::onSetDisplayIdentity(const char* name, const char* description) {
    const SDisplayIdentity DISPLAY = {.name = name, .description = description};

    if (DISPLAY.name.size() > MAX_DISPLAY_NAME_LENGTH || DISPLAY.description.size() > MAX_DISPLAY_DESCRIPTION_LENGTH) {
        m_resource->error(Arguments::MALFORMED, "display identity is too long");
        return;
    }

    if (m_state == eState::UNAVAILABLE)
        return;

    m_context->m_display = DISPLAY;

    if (m_state == eState::PERSISTENT)
        save(m_token, std::nullopt, [](bool) {});
}

void CPermissionContextResource::onRequestPermission(const char* permission, uint32_t mode) {
    if (mode > HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT || !isRequestablePermission(permission)) {
        m_resource->error(Arguments::MALFORMED, "invalid permission request");
        return;
    }

    // inert, no results are sent anymore
    if (m_state == eState::UNAVAILABLE)
        return;

    const std::string PERMISSION = permission;

    if (!m_context->beginRequest(PERMISSION)) {
        m_resource->error(Arguments::MALFORMED, std::format("a request for {} is already pending", PERMISSION));
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
    save(m_token, permission, [this, permission](bool stored) {
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
