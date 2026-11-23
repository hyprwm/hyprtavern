#include "SavedContext.hpp"

#include <common/Memory.hpp>
#include <common/PermissionName.hpp>

#include <glaze/glaze.hpp>

#include <format>
#include <unordered_set>

using namespace Security;

namespace {
    constexpr uint32_t SAVED_CONTEXT_VERSION = 1;

    // see hp_hyprtavern_kv_store_v1
    constexpr size_t MAX_KEY_LENGTH = 1024;

    bool             isKeyCharacter(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || std::string_view{":/.,-+="}.contains(c);
    }

    struct SSavedContextRecord {
        uint32_t                 version   = SAVED_CONTEXT_VERSION;
        uint32_t                 ownerKind = 0;
        std::string              ownerId;
        std::string              displayName;
        std::string              displayDescription;
        std::vector<std::string> grants;
    };

    bool validGrants(const std::vector<std::string>& grants) {
        if (grants.size() > MAX_GRANTS)
            return false;

        std::unordered_set<std::string_view> seen;

        for (const auto& grant : grants) {
            // grants from a newer catalog are kept, unknown core permissions are never satisfied anyway
            if (!isRequestablePermission(grant) || grant == PERMISSION_UNSANDBOXED || !seen.emplace(grant).second)
                return false;
        }

        return true;
    }
}

std::string Security::serializeSavedContext(const SSavedContext& context) {
    const SSavedContextRecord RECORD = {
        .ownerKind          = sc<uint32_t>(context.owner.kind),
        .ownerId            = context.owner.id,
        .displayName        = context.display.name,
        .displayDescription = context.display.description,
        .grants             = context.grants,
    };

    return glz::write_json(RECORD).value_or("");
}

std::optional<SSavedContext> Security::parseSavedContext(std::string_view data) {
    const auto RECORD = glz::read_json<SSavedContextRecord>(data);
    if (!RECORD || RECORD->version != SAVED_CONTEXT_VERSION)
        return std::nullopt;

    const SAppIdentity OWNER = {.kind = sc<eIdentityKind>(RECORD->ownerKind), .id = RECORD->ownerId, .stable = true};

    if (RECORD->ownerKind > sc<uint32_t>(eIdentityKind::HOST_APPLICATION) || !OWNER.persistable() || !validGrants(RECORD->grants))
        return std::nullopt;

    return SSavedContext{
        .owner   = OWNER,
        .display = {.name = RECORD->displayName, .description = RECORD->displayDescription},
        .grants  = RECORD->grants,
    };
}

std::optional<std::string> Security::savedContextKey(const SAppIdentity& owner) {
    std::string key = std::format("context:{}:", sc<uint32_t>(owner.kind));

    // ids may contain anything, so whatever keys don't allow is escaped. ? is never kept, so this is unambiguous.
    for (const auto C : owner.id) {
        if (isKeyCharacter(C))
            key += C;
        else
            key += std::format("?{:02x}", sc<uint8_t>(C));
    }

    if (key.size() > MAX_KEY_LENGTH)
        return std::nullopt;

    return key;
}
