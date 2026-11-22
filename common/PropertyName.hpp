#pragma once

#include <string_view>

namespace Bus {
    // (group):(name), both nonempty and made of letters, numbers, _ or +
    bool isValidPropertyName(std::string_view name);
}
