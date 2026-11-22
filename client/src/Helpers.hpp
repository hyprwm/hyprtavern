#pragma once

#include <hyprtavern-client/Types.hpp>

#include <chrono>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Hyprtavern {
    using Clock = std::chrono::steady_clock;

    // strings with a NUL would be silently truncated on the wire
    bool      validString(std::string_view string);
    bool      validStrings(const std::vector<std::string>& strings);
    bool      validPermissions(const std::vector<std::string>& permissions);

    SIdentity identityFromWire(const char* id, uint32_t kind, uint32_t stable);

    // dispatches whenever fd is readable, until done returns true. Fails once the deadline passes, or dispatch fails.
    std::expected<void, eError> dispatchUntil(int fd, const std::function<bool()>& dispatch, const std::function<bool()>& done, Clock::time_point deadline);
}
