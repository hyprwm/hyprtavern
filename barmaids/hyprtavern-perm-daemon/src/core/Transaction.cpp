#include "Transaction.hpp"

#include <common/Memory.hpp>
#include <common/PermissionName.hpp>

using namespace Prompts;

constexpr uint32_t ASK_TYPE_PERSIST = 1;

namespace {
    std::unexpected<STransactionError> fail(eTransactionError code, std::string message) {
        return std::unexpected(STransactionError{.code = code, .message = std::move(message)});
    }
}

std::expected<void, STransactionError> CTransactionState::setDisplayName(std::string name) {
    if (m_asked)
        return fail(eTransactionError::INVALID_STATE, "the transaction is frozen");

    m_request.displayName = std::move(name);
    return {};
}

std::expected<void, STransactionError> CTransactionState::setAttestedID(std::string id, uint32_t kind) {
    if (m_asked)
        return fail(eTransactionError::INVALID_STATE, "the transaction is frozen");

    if (kind > sc<uint32_t>(eIdentityKind::HOST_APPLICATION))
        return fail(eTransactionError::INVALID_ARGUMENT, "invalid identity kind");

    // unknown identities have no identifier, all others need one
    if ((kind == sc<uint32_t>(eIdentityKind::UNKNOWN)) != id.empty())
        return fail(eTransactionError::INVALID_ARGUMENT, "identity kind and identifier don't match");

    m_request.attestedID = std::move(id);
    m_request.kind       = sc<eIdentityKind>(kind);
    m_hasIdentity        = true;
    return {};
}

std::expected<void, STransactionError> CTransactionState::ask(std::string permission, uint32_t askType) {
    if (m_asked)
        return fail(eTransactionError::INVALID_STATE, "ask_permission may only be sent once");

    if (!m_hasIdentity)
        return fail(eTransactionError::INVALID_STATE, "the attested identity has to be set first");

    if (askType > ASK_TYPE_PERSIST)
        return fail(eTransactionError::INVALID_ARGUMENT, "invalid ask type");

    if (!Security::isPromptablePermission(permission))
        return fail(eTransactionError::INVALID_PERMISSION, "the permission cannot be prompted for");

    m_request.permission = std::move(permission);
    m_request.persist    = askType == ASK_TYPE_PERSIST;
    m_asked              = true;
    return {};
}

bool CTransactionState::asked() const {
    return m_asked;
}

const SRequest& CTransactionState::request() const {
    return m_request;
}
