#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#define HYPRTAVERN_API __attribute__((visibility("default")))

namespace Hyprtavern {
    enum class eError : uint8_t {
        NO_TAVERN,
        HANDSHAKE_FAILED,
        UNSUPPORTED,
        TIMED_OUT,
        DISCONNECTED,
        INVALID_ARGUMENT,
        NOT_FOUND,
        BAD_PROTOCOL,
        INSUFFICIENT_PERMISSIONS,
        DENIED,
        BUS_FULL,
        UNKNOWN,
    };

    // values match application_identity_kind in hp_hyprtavern_core_v1
    enum class eIdentityKind : uint8_t {
        UNKNOWN         = 0,
        HOST_EXECUTABLE = 1,
        FLATPAK         = 2,
        INTERNAL        = 3,
    };

    // values match bus_query_filter_mode in hp_hyprtavern_core_v1
    enum class eFilterMode : uint8_t {
        ALL = 0,
        ANY = 1,
    };

    // values match security_permission_mode in hp_hyprtavern_core_v1
    enum class ePermissionMode : uint8_t {
        SESSION   = 0,
        PERMANENT = 1,
    };

    enum class ePermissionResult : uint8_t {
        GRANTED,
        DENIED,
        FAILED,
    };

    /*
        A tavern-attested application identity. Kind and id together identify an application,
        a display name or a PID never do.
    */
    struct SIdentity {
        eIdentityKind kind = eIdentityKind::UNKNOWN;
        std::string   id;
        bool          stable = false;
    };

    constexpr std::chrono::milliseconds DEFAULT_TIMEOUT = std::chrono::seconds(10);

    // permission requests may wait for the user
    constexpr std::chrono::milliseconds PROMPT_TIMEOUT = std::chrono::minutes(1);

    HYPRTAVERN_API std::string_view errorString(eError error);
}
