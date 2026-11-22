#include "Helpers.hpp"

#include <common/Limits.hpp>
#include <common/Memory.hpp>
#include <common/PermissionName.hpp>

#include <algorithm>
#include <cerrno>
#include <limits>

#include <poll.h>

using namespace Hyprtavern;

bool Hyprtavern::validString(std::string_view string) {
    return !string.contains('\0');
}

bool Hyprtavern::validStrings(const std::vector<std::string>& strings) {
    return strings.size() <= MAX_ARRAY_ELEMENTS && std::ranges::all_of(strings, [](const auto& string) { return validString(string); });
}

bool Hyprtavern::validPermissions(const std::vector<std::string>& permissions) {
    return permissions.size() <= MAX_ARRAY_ELEMENTS && std::ranges::all_of(permissions, [](const auto& permission) { return Security::isValidPermission(permission); });
}

SIdentity Hyprtavern::identityFromWire(const char* id, uint32_t kind, uint32_t stable) {
    // kinds this library doesn't know are no identity it could reason about
    if (kind > sc<uint32_t>(eIdentityKind::INTERNAL))
        return {};

    return {.kind = sc<eIdentityKind>(kind), .id = id, .stable = stable != 0};
}

std::expected<void, eError> Hyprtavern::dispatchUntil(int fd, const std::function<bool()>& dispatch, const std::function<bool()>& done, Clock::time_point deadline) {
    while (!done()) {
        const auto REMAINING = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (REMAINING <= 0)
            return std::unexpected(eError::TIMED_OUT);

        pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
        if (poll(&pfd, 1, sc<int>(std::min<int64_t>(REMAINING, std::numeric_limits<int>::max()))) < 0 && errno != EINTR)
            return std::unexpected(eError::DISCONNECTED);

        // the last events may still have completed it
        if (!dispatch() && !done())
            return std::unexpected(eError::DISCONNECTED);
    }

    return {};
}
