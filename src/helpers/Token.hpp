#pragma once

#include <string>
#include <string_view>

namespace Token {
    // an unpredictable 128-bit token, as lowercase hex
    std::string generate();

    // whether the token has the format produced by generate()
    bool isWellFormed(std::string_view token);
}
