#pragma once

#include <string>
#include <vector>

namespace Wire {
    inline std::vector<std::string> toStrings(const std::vector<const char*>& strings) {
        return {strings.begin(), strings.end()};
    }

    // the result points into strings, which must outlive it
    inline std::vector<const char*> toCStrings(const std::vector<std::string>& strings) {
        std::vector<const char*> result;
        result.reserve(strings.size());

        for (const auto& string : strings) {
            result.emplace_back(string.c_str());
        }

        return result;
    }
}
