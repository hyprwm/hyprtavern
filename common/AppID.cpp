#include "AppID.hpp"

#include <algorithm>
#include <ranges>

namespace {
    constexpr size_t MAX_APP_ID_LENGTH = 255;

    bool             isValidElement(std::string_view element) {
        if (element.empty() || (element.front() >= '0' && element.front() <= '9'))
            return false;

        return std::ranges::all_of(element, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
    }
}

bool Security::isValidAppID(std::string_view id) {
    if (id.size() > MAX_APP_ID_LENGTH || !id.contains('.'))
        return false;

    return std::ranges::all_of(id | std::views::split('.'), [](auto&& element) { return isValidElement(std::string_view{element}); });
}
