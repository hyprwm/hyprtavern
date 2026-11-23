#pragma once

#include "Identity.hpp"
#include "PermissionContext.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Security {
    // the permanent grants of an application, owned by its stable attested identity
    struct SSavedContext {
        SAppIdentity             owner;
        SDisplayIdentity         display;
        std::vector<std::string> grants;
    };

    std::string serializeSavedContext(const SSavedContext& context);

    // rejects malformed records, unknown versions, unstable owners and invalid or excessive grants.
    std::optional<SSavedContext> parseSavedContext(std::string_view data);

    // the kv key of an owner's saved context, nullopt if its identity is too long for one
    std::optional<std::string> savedContextKey(const SAppIdentity& owner);
}
