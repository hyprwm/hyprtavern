#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <sys/types.h>

namespace Security {
    // values match application_identity_kind in hp_hyprtavern_core_v1
    enum class eIdentityKind : uint8_t {
        UNKNOWN         = 0,
        HOST_EXECUTABLE = 1,
        FLATPAK         = 2,
        INTERNAL        = 3,
    };

    // a tavern-attested application identity. Kind and id together identify an application.
    struct SAppIdentity {
        eIdentityKind kind = eIdentityKind::UNKNOWN;
        std::string   id;
        bool          stable = false;

        // only stable identities may own persistent, app-scoped state
        bool persistable() const;

        bool operator==(const SAppIdentity&) const = default;
    };

    // attests a peer process from its socket credentials. Anything ambiguous is reported as unknown.
    SAppIdentity               probeProcess(pid_t pid, uid_t uid, const std::filesystem::path& procfs = "/proc");

    std::optional<std::string> parseFlatpakAppID(std::string_view flatpakInfo);
}
