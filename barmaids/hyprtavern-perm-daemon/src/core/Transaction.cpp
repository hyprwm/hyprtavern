#include "Transaction.hpp"

#include <common/Memory.hpp>
#include <common/PermissionName.hpp>

using namespace Prompts;

constexpr uint32_t               ASK_TYPE_PERSIST = 1;

std::expected<void, std::string> CTransactionState::setDisplayName(std::string name) {
    if (m_asked)
        return std::unexpected("the transaction is frozen");

    m_request.displayName = std::move(name);
    return {};
}

std::expected<void, std::string> CTransactionState::setAttestedID(std::string id, uint32_t kind) {
    if (m_asked)
        return std::unexpected("the transaction is frozen");

    if (kind > sc<uint32_t>(eIdentityKind::INTERNAL))
        return std::unexpected("invalid identity kind");

    // unknown identities have no identifier, all others need one
    if ((kind == sc<uint32_t>(eIdentityKind::UNKNOWN)) != id.empty())
        return std::unexpected("identity kind and identifier don't match");

    m_request.attestedID = std::move(id);
    m_request.kind       = sc<eIdentityKind>(kind);
    m_hasIdentity        = true;
    return {};
}

std::expected<void, std::string> CTransactionState::ask(std::string permission, uint32_t askType) {
    if (m_asked)
        return std::unexpected("ask_permission may only be sent once");

    if (!m_hasIdentity)
        return std::unexpected("the attested identity has to be set first");

    if (askType > ASK_TYPE_PERSIST)
        return std::unexpected("invalid ask type");

    if (!Security::isPromptablePermission(permission))
        return std::unexpected("the permission cannot be prompted for");

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
