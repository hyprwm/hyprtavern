#include "PropertyName.hpp"

#include <algorithm>

namespace {
    bool isValidPropertyPart(std::string_view part) {
        if (part.empty())
            return false;

        return std::ranges::all_of(part, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '+'; });
    }
}

bool Bus::isValidPropertyName(std::string_view name) {
    const auto COLON = name.find(':');
    if (COLON == std::string_view::npos)
        return false;

    return isValidPropertyPart(name.substr(0, COLON)) && isValidPropertyPart(name.substr(COLON + 1));
}
