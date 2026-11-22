#include "PermissionBarmaid.hpp"

#include <common/Log.hpp>

using namespace Barmaids;

constexpr uint32_t PERMISSION_AUTHENTICATION_PROTOCOL_VERSION = 1;

namespace {
    CPermissionBarmaid::eResult parseResult(std::string_view name, uint32_t result, std::string_view permission, bool persist) {
        if (name != permission) {
            g_logger->log(LOG_ERR, "the permission barmaid answered for {} instead of {}", name, permission);
            return CPermissionBarmaid::eResult::FAILED;
        }

        switch (result) {
            case HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_ONCE: return CPermissionBarmaid::eResult::ACCEPTED_SESSION;
            case HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_PERSISTENT:
                if (persist)
                    return CPermissionBarmaid::eResult::ACCEPTED_PERSISTENT;

                g_logger->log(LOG_ERR, "the permission barmaid granted {} persistently without being asked to", permission);
                return CPermissionBarmaid::eResult::FAILED;
            case HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED: return CPermissionBarmaid::eResult::DENIED;
            default: return CPermissionBarmaid::eResult::FAILED;
        }
    }
}

CPermissionBarmaid::CPrompt::CPrompt(SP<CTransaction> transaction) : m_transaction(std::move(transaction)) {
    ;
}

CPermissionBarmaid::CPrompt::~CPrompt() {
    if (!m_done && Barmaids::permissions)
        Barmaids::permissions->dismiss(m_transaction);
}

CPermissionBarmaid::CPermissionBarmaid() : CBarmaid("hyprtavern-perm-daemon", Bus::eProviderRole::PERMISSION_AUTHENTICATION, false) {
    ;
}

void CPermissionBarmaid::onConnected() {
    auto       impl = makeShared<CCHpHyprtavernPermissionAuthenticationV1Impl>(PERMISSION_AUTHENTICATION_PROTOCOL_VERSION);
    const auto SPEC = impl->protocol();
    m_control->addImplementation(std::move(impl));

    m_control->bindProtocolAsync(SPEC, PERMISSION_AUTHENTICATION_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> object) {
        if (!object) {
            g_logger->log(LOG_ERR, "the permission barmaid does not provide prompts, permission requests will fail");
            return;
        }

        m_manager = makeShared<CCHpHyprtavernPermissionAuthenticationManagerV1Object>(std::move(object));
        m_manager->setAvailability([this](uint32_t available) {
            g_logger->log(LOG_DEBUG, "permission prompts are {}", available ? "available" : "unavailable");
            m_available = available != 0;
        });
    });
}

bool CPermissionBarmaid::available() const {
    return m_manager && m_available && ready();
}

UP<CPermissionBarmaid::CPrompt> CPermissionBarmaid::ask(const Security::SAppIdentity& identity, const std::string& displayName, const std::string& permission, bool persist,
                                                        std::function<void(eResult)>&& callback) {
    if (!available())
        return nullptr;

    auto object = m_manager->sendInitPermissionTransaction();
    if (!object)
        return nullptr;

    auto transaction = makeShared<CTransaction>(std::move(object));
    auto prompt      = makeUnique<CPrompt>(transaction);

    // the transaction is owned by the prompt, so the prompt is alive whenever this runs
    transaction->setPermissionResult([prompt = prompt.get(), permission, persist, callback = std::move(callback)](const char* name, uint32_t result) mutable {
        prompt->m_done = true;

        // running the callback may drop the prompt
        auto onResult = std::move(callback);
        onResult(parseResult(name, result, permission, persist));
    });

    if (!displayName.empty())
        transaction->sendSetRequestingAppDisplayName(displayName.c_str());

    transaction->sendSetRequestingAppAttestedId(identity.id.c_str(), sc<hpHyprtavernPermissionAuthenticationV1ApplicationIdentityKind>(identity.kind));
    transaction->sendAskPermission(permission.c_str(),
                                   persist ? HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_ASK_TYPE_PERSIST : HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_ASK_TYPE_SESSION);

    return prompt;
}

void CPermissionBarmaid::dismiss(SP<CTransaction> transaction) {
    transaction->setPermissionResult([](const char*, uint32_t) {});
    transaction->sendDestroy();

    // a result sent before the barmaid saw the destroy may still arrive, so keep the object until then
    sync([transaction] {});
}
