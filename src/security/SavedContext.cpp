#include "SavedContext.hpp"

#include <common/Memory.hpp>
#include <common/PermissionName.hpp>

#include <glaze/glaze.hpp>

#include <unordered_set>

using namespace Security;

namespace {
    constexpr uint32_t SAVED_CONTEXT_VERSION = 1;

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
            if (!isPromptablePermission(grant) || !seen.emplace(grant).second)
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

    if (RECORD->ownerKind > sc<uint32_t>(eIdentityKind::INTERNAL) || !OWNER.persistable() || !validGrants(RECORD->grants))
        return std::nullopt;

    return SSavedContext{
        .owner   = OWNER,
        .display = {.name = RECORD->displayName, .description = RECORD->displayDescription},
        .grants  = RECORD->grants,
    };
}
