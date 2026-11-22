#include "Environment.hpp"
#include "Limits.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string_view>
#include <unordered_set>

std::expected<void, std::string> Environment::validateUpdate(const std::vector<const char*>& names, const std::vector<const char*>& values) {
    if (names.size() > MAX_ARRAY_ELEMENTS || values.size() > MAX_ARRAY_ELEMENTS)
        return std::unexpected("environment arrays are too large");

    if (names.size() != values.size())
        return std::unexpected("environment arrays have mismatched lengths");

    std::unordered_set<std::string_view> seen;

    for (const std::string_view name : names) {
        if (name.empty())
            return std::unexpected("environment variable name is empty");

        if (name.contains('='))
            return std::unexpected(std::format("environment variable name {} contains '='", name));

        if (!seen.emplace(name).second)
            return std::unexpected(std::format("environment variable {} occurs more than once", name));
    }

    return {};
}

void Environment::applyUpdate(const std::vector<const char*>& names, const std::vector<const char*>& values) {
    for (size_t i = 0; i < names.size(); ++i) {
        if (std::string_view{values[i]}.empty())
            unsetenv(names[i]);
        else
            setenv(names[i], values[i], 1);
    }
}
