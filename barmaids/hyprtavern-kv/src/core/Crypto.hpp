#pragma once

#include <expected>
#include <string>

namespace Crypto {
    std::expected<std::string, std::string> encrypt(const std::string& data, const std::string& password);
    std::expected<std::string, std::string> decrypt(const std::string& data, const std::string& password);
}
