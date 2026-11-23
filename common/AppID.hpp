#pragma once

#include <string_view>

namespace Security {
    // a reverse-DNS application ID, e.g. org.example.App, as host applications may declare
    bool isValidAppID(std::string_view id);
}
