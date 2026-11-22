#pragma once

#include "Identity.hpp"
#include "PermissionContext.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Security {
    // a permission context persisted under a token, owned by a stable attested identity
    struct SSavedContext {
        SAppIdentity             owner;
        SDisplayIdentity         display;
        std::vector<std::string> grants;
    };

    std::string serializeSavedContext(const SSavedContext& context);

    // rejects malformed records, unknown versions, unstable owners and invalid or excessive grants.
    std::optional<SSavedContext> parseSavedContext(std::string_view data);
}
