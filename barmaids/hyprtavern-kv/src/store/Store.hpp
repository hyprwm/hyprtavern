#pragma once

#include <common/Memory.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Kv {
    constexpr size_t MAX_KEY_LENGTH   = 1024;
    constexpr size_t MAX_VALUE_LENGTH = 1024 * 1024;
    constexpr size_t MAX_ENTRIES      = 65536;

    // nonempty, made of ASCII letters, numbers and :/.,-+=?
    bool                                              isValidKey(std::string_view key);

    std::expected<std::filesystem::path, std::string> defaultPath();

    // a plaintext store, split into independent scopes. Every change is committed durably.
    class CStore {
      public:
        // a missing file is an empty store. A malformed one is an error, it is never overwritten.
        static std::expected<UP<CStore>, std::string> open(const std::filesystem::path& path);

        std::optional<std::string>                    get(const std::string& scope, const std::string& key) const;

        // whether there is room for a write. A full store only refuses new keys.
        bool fits(const std::string& scope, const std::string& key, const std::string& value) const;

        // an empty value deletes the key. Failures leave the store unchanged.
        std::expected<void, std::string> set(const std::string& scope, const std::string& key, const std::string& value);

      private:
        CStore(std::filesystem::path path);

        std::expected<void, std::string>                          save() const;

        std::filesystem::path                                     m_path;
        std::map<std::string, std::map<std::string, std::string>> m_scopes;
        size_t                                                    m_entries = 0;
    };
}
