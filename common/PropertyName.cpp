#include "PropertyName.hpp"
#include "Limits.hpp"

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
    if (name.size() > MAX_PROPERTY_NAME_LENGTH || COLON == std::string_view::npos)
        return false;

    return isValidPropertyPart(name.substr(0, COLON)) && isValidPropertyPart(name.substr(COLON + 1));
}

bool Bus::isValidPropertyValue(std::string_view value) {
    return value.size() <= MAX_PROPERTY_VALUE_LENGTH;
}
