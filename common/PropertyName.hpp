#pragma once

#include <string_view>

namespace Bus {
    // (group):(name), both nonempty and made of letters, numbers, _ or +
    bool isValidPropertyName(std::string_view name);

    // any string short enough, an empty value removes a property
    bool isValidPropertyValue(std::string_view value);
}
