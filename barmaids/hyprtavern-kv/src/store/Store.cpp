#include "Store.hpp"

#include <glaze/glaze.hpp>

#include <hyprutils/os/FileDescriptor.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Kv;
using namespace Hyprutils::OS;

namespace {
    constexpr uint32_t STORE_VERSION = 1;

    struct SStoreFile {
        uint32_t                                                  version = 0;
        std::map<std::string, std::map<std::string, std::string>> scopes;
    };

    std::string errnoMessage(std::string_view what) {
        return std::format("{}: {}", what, strerror(errno));
    }

    bool writeAll(int fd, std::string_view data) {
        while (!data.empty()) {
            const auto RET = write(fd, data.data(), data.size());
            if (RET < 0 && errno == EINTR)
                continue;

            if (RET <= 0)
                return false;

            data.remove_prefix(RET);
        }

        return true;
    }

    // sandboxed apps must not reach the backing file, so the directory has to be private
    std::expected<void, std::string> prepareDirectory(const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return std::unexpected(std::format("failed to create {}: {}", dir.string(), ec.message()));

        const auto STATUS = std::filesystem::symlink_status(dir, ec);
        if (ec || std::filesystem::is_symlink(STATUS) || !std::filesystem::is_directory(STATUS))
            return std::unexpected(std::format("{} is not a directory", dir.string()));

        if (chmod(dir.c_str(), S_IRWXU) < 0)
            return std::unexpected(errnoMessage("failed to make the store directory private"));

        return {};
    }
}

bool Kv::isValidKey(std::string_view key) {
    constexpr std::string_view EXTRA_CHARACTERS = ":/.,-+=?";

    if (key.empty() || key.size() > MAX_KEY_LENGTH)
        return false;

    return std::ranges::all_of(key,
                               [&EXTRA_CHARACTERS](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || EXTRA_CHARACTERS.contains(c); });
}

std::expected<std::filesystem::path, std::string> Kv::defaultPath() {
    std::filesystem::path dataHome;

    if (const char* xdgDataHome = getenv("XDG_DATA_HOME"); xdgDataHome && std::filesystem::path{xdgDataHome}.is_absolute())
        dataHome = xdgDataHome;
    else if (const char* home = getenv("HOME"); home && *home)
        dataHome = std::filesystem::path{home} / ".local" / "share";
    else
        return std::unexpected("neither XDG_DATA_HOME nor HOME are set");

    return dataHome / "hyprtavern" / "kv.json";
}

CStore::CStore(std::filesystem::path path) : m_path(std::move(path)) {
    ;
}

std::expected<UP<CStore>, std::string> CStore::open(const std::filesystem::path& path) {
    if (const auto RET = prepareDirectory(path.parent_path()); !RET)
        return std::unexpected(RET.error());

    auto            store = UP<CStore>(new CStore(path));

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return store;

    std::ifstream file(path);
    if (!file.good())
        return std::unexpected(errnoMessage(std::format("failed to read {}", path.string())));

    std::stringstream contents;
    contents << file.rdbuf();

    SStoreFile parsed;
    if (glz::read_json(parsed, contents.str()) || parsed.version != STORE_VERSION)
        return std::unexpected(std::format("{} is malformed", path.string()));

    store->m_scopes = std::move(parsed.scopes);

    for (const auto& [scope, entries] : store->m_scopes) {
        store->m_entries += entries.size();
    }

    return store;
}

std::optional<std::string> CStore::get(const std::string& scope, const std::string& key) const {
    const auto SCOPE = m_scopes.find(scope);
    if (SCOPE == m_scopes.end())
        return std::nullopt;

    const auto ENTRY = SCOPE->second.find(key);
    if (ENTRY == SCOPE->second.end())
        return std::nullopt;

    return ENTRY->second;
}

std::expected<void, std::string> CStore::set(const std::string& scope, const std::string& key, const std::string& value) {
    if (value.size() > MAX_VALUE_LENGTH)
        return std::unexpected("value is too large");

    const auto PREVIOUS = get(scope, key);

    // deleting a missing key succeeds, it's already durable
    if (value.empty() && !PREVIOUS)
        return {};

    if (!PREVIOUS && m_entries >= MAX_ENTRIES)
        return std::unexpected("the store is full");

    if (value.empty())
        m_scopes[scope].erase(key);
    else
        m_scopes[scope][key] = value;

    if (auto RET = save(); !RET) {
        if (PREVIOUS)
            m_scopes[scope][key] = *PREVIOUS;
        else
            m_scopes[scope].erase(key);

        if (m_scopes[scope].empty())
            m_scopes.erase(scope);

        return RET;
    }

    if (value.empty())
        --m_entries;
    else if (!PREVIOUS)
        ++m_entries;

    if (m_scopes[scope].empty())
        m_scopes.erase(scope);

    return {};
}

std::expected<void, std::string> CStore::save() const {
    const auto JSON = glz::write_json(glz::obj{"version", STORE_VERSION, "scopes", m_scopes});
    if (!JSON)
        return std::unexpected("failed to serialize the store");

    const auto TEMPORARY = m_path.string() + ".tmp";

    {
        const CFileDescriptor FD{::open(TEMPORARY.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600)};
        if (!FD.isValid())
            return std::unexpected(errnoMessage("failed to create the temporary store file"));

        if (!writeAll(FD.get(), *JSON) || fsync(FD.get()) < 0) {
            const auto ERROR = errnoMessage("failed to write the store");
            unlink(TEMPORARY.c_str());
            return std::unexpected(ERROR);
        }
    }

    if (rename(TEMPORARY.c_str(), m_path.c_str()) < 0) {
        const auto ERROR = errnoMessage("failed to replace the store");
        unlink(TEMPORARY.c_str());
        return std::unexpected(ERROR);
    }

    // the rename itself has to be durable too
    const CFileDescriptor DIR{::open(m_path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (!DIR.isValid() || fsync(DIR.get()) < 0)
        return std::unexpected(errnoMessage("failed to sync the store directory"));

    return {};
}
