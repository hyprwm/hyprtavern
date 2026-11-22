#include "Token.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>

namespace {
    constexpr size_t TOKEN_BYTES = 16;
}

std::string Token::generate() {
    std::array<uint8_t, TOKEN_BYTES> bytes;
    arc4random_buf(bytes.data(), bytes.size());

    std::string token;
    token.reserve(TOKEN_BYTES * 2);

    for (const auto BYTE : bytes) {
        token += std::format("{:02x}", BYTE);
    }

    return token;
}

bool Token::isWellFormed(std::string_view token) {
    return token.size() == TOKEN_BYTES * 2 && std::ranges::all_of(token, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
