#include "Kv.hpp"
#include "Core.hpp"

#include "../helpers/Logger.hpp"
#include "../ui/GUI.hpp"

#include "Crypto.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include <sys/stat.h>
#include <unistd.h>

#include <glaze/glaze.hpp>

constexpr const char*  KV_STORE_FILE_NAME   = "hyprtavern-kv.dat";
constexpr const char*  TAVERN_DATA_DIR_NAME = "hyprtavern";

constexpr const size_t MAX_KEY_SIZE            = 1024;
constexpr const size_t MAX_VALUE_SIZE          = 1024 * 1024;
constexpr const size_t MAX_APP_IDENTIFIER_SIZE = 1024;
constexpr const size_t MAX_PASSWORD_SIZE       = 4096;
constexpr const size_t MAX_APPS                = 4096;
constexpr const size_t MAX_ENTRIES             = 16384;
constexpr const size_t MAX_SERIALIZED_SIZE     = 8 * 1024 * 1024;

CKvStore::~CKvStore() {
    if (!m_initThread.joinable())
        return;

    m_initThread.request_stop();
    m_initThread.join();
}

std::expected<std::filesystem::path, std::string> CKvStore::storePath() {
    std::filesystem::path dataHome;

    if (const char* xdgDataHome = getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
        dataHome = std::string{xdgDataHome};
        if (!dataHome.is_absolute())
            dataHome.clear();
    }

    if (dataHome.empty()) {
        const char* home = getenv("HOME");
        if (!home || !*home)
            return std::unexpected("neither $XDG_DATA_HOME nor $HOME is available");

        dataHome = std::filesystem::path{std::string{home}} / ".local" / "share";
    }

    return dataHome / TAVERN_DATA_DIR_NAME / KV_STORE_FILE_NAME;
}

std::expected<void, std::string> CKvStore::prepareStoreDirectory(const std::filesystem::path& path) {
    const auto      DIR = path.parent_path();
    std::error_code ec;
    auto            status = std::filesystem::symlink_status(DIR, ec);

    if (ec && ec != std::errc::no_such_file_or_directory)
        return std::unexpected(std::format("failed to inspect store directory: {}", ec.message()));

    if (std::filesystem::exists(status)) {
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
            return std::unexpected("store directory is not a private directory");
    } else {
        std::filesystem::create_directories(DIR, ec);
        if (ec)
            return std::unexpected(std::format("failed to create store directory: {}", ec.message()));
    }

    if (chmod(DIR.c_str(), S_IRWXU) < 0)
        return std::unexpected(std::format("failed to secure store directory: {}", std::strerror(errno)));

    return {};
}

std::expected<void, std::string> CKvStore::validateKey(const std::string_view& key) {
    if (key.empty())
        return std::unexpected("key must not be empty");
    if (key.size() > MAX_KEY_SIZE)
        return std::unexpected("key is too large");
    static constexpr std::string_view EXTRA_KEY_CHARS = ":/.,-+=?";
    if (!std::ranges::all_of(key, [](const unsigned char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
                EXTRA_KEY_CHARS.contains(static_cast<char>(character));
        }))
        return std::unexpected("key contains characters outside the protocol allowlist");

    return {};
}

std::expected<void, std::string> CKvStore::validateStorage(const SKvStorage& storage) {
    if (storage.apps.size() > MAX_APPS)
        return std::unexpected("application quota exceeded");

    size_t entryCount = 0;

    auto   validateEntries = [&entryCount](const std::vector<SKvEntry>& entries) -> std::expected<void, std::string> {
        if (entries.size() > MAX_ENTRIES - entryCount)
            return std::unexpected("entry quota exceeded");

        entryCount += entries.size();
        std::unordered_set<std::string_view> keys;
        keys.reserve(entries.size());

        for (const auto& entry : entries) {
            if (auto valid = validateKey(entry.key); !valid)
                return valid;
            if (entry.value.size() > MAX_VALUE_SIZE)
                return std::unexpected("value is too large");
            if (!keys.emplace(entry.key).second)
                return std::unexpected("store contains a duplicate key");
        }

        return {};
    };

    if (auto valid = validateEntries(storage.global); !valid)
        return valid;
    if (auto valid = validateEntries(storage.tavern); !valid)
        return valid;

    std::unordered_set<std::string_view> appIdentifiers;
    appIdentifiers.reserve(storage.apps.size());

    for (const auto& app : storage.apps) {
        if (app.appName.empty() || app.appName.size() > MAX_APP_IDENTIFIER_SIZE || app.appName.find('\0') != std::string::npos)
            return std::unexpected("store contains an invalid application identifier");
        if (!appIdentifiers.emplace(app.appName).second)
            return std::unexpected("store contains a duplicate application identifier");
        if (auto valid = validateEntries(app.entries); !valid)
            return valid;
    }

    return {};
}

std::expected<void, std::string> CKvStore::saveStorage(const std::filesystem::path& path, const SKvStorage& storage, const std::string& password) {
    if (auto valid = validateStorage(storage); !valid)
        return valid;

    auto serialized = glz::write_json(storage);
    if (!serialized)
        return std::unexpected("failed to serialize kv data");
    if (serialized->size() > MAX_SERIALIZED_SIZE)
        return std::unexpected("store size quota exceeded");

    Crypto::CEncryptedBlob blob(*serialized, password);
    if (blob.result() != Crypto::CRYPTO_RESULT_OK)
        return std::unexpected("failed to encrypt kv data");

    return blob.writeToFile(path);
}

CKvStore::eKvStoreInitResult CKvStore::loadFromDisk(const std::stop_token& token, const std::filesystem::path& path, SKvStorage& storage, std::string& password,
                                                    std::string& error) {
    std::error_code ec;
    const auto      status = std::filesystem::symlink_status(path, ec);

    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = std::format("failed to inspect kv store: {}", ec.message());
        return KV_STORE_INIT_UNKNOWN_ERROR;
    }

    if (!std::filesystem::exists(status)) {
        if (token.stop_requested()) {
            error = "initialization cancelled";
            return KV_STORE_INIT_CANCELLED;
        }

        g_logger->log(LOG_DEBUG, "kv store missing: starting first-time setup");
        auto setup = GUI::firstTimeSetup();
        if (!setup) {
            error = setup.error();
            return KV_STORE_INIT_CANCELLED;
        }
        if (setup->size() > MAX_PASSWORD_SIZE) {
            error = "password is too large";
            return KV_STORE_INIT_UNKNOWN_ERROR;
        }
        if (token.stop_requested()) {
            error = "initialization cancelled";
            return KV_STORE_INIT_CANCELLED;
        }

        password = std::move(*setup);
        if (auto saved = saveStorage(path, storage, password); !saved) {
            error = saved.error();
            return KV_STORE_INIT_UNKNOWN_ERROR;
        }

        return KV_STORE_INIT_OK;
    }

    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        error = "kv store is not a regular file";
        return KV_STORE_INIT_UNKNOWN_ERROR;
    }

    if (chmod(path.c_str(), S_IRUSR | S_IWUSR) < 0) {
        error = std::format("failed to secure kv store: {}", std::strerror(errno));
        return KV_STORE_INIT_UNKNOWN_ERROR;
    }

    Crypto::CEncryptedBlob blob(path, password);
    bool                   migrateLegacyPassword = false;
    if (blob.result() == Crypto::CRYPTO_RESULT_BAD_PW) {
        Crypto::CEncryptedBlob legacyBlob(path, "vaxwashere");
        if (legacyBlob.result() == Crypto::CRYPTO_RESULT_OK && legacyBlob.version() == 1) {
            blob = std::move(legacyBlob);
            password.clear();
            migrateLegacyPassword = true;
        }
    }

    while (blob.result() == Crypto::CRYPTO_RESULT_BAD_PW) {
        if (token.stop_requested()) {
            error = "initialization cancelled";
            return KV_STORE_INIT_CANCELLED;
        }

        auto enteredPassword = GUI::passwordAsk();
        if (!enteredPassword) {
            error = enteredPassword.error();
            return KV_STORE_INIT_CANCELLED;
        }
        if (enteredPassword->size() > MAX_PASSWORD_SIZE) {
            error = "password is too large";
            return KV_STORE_INIT_UNKNOWN_ERROR;
        }

        password = std::move(*enteredPassword);
        blob     = Crypto::CEncryptedBlob(path, password);
    }

    if (blob.result() != Crypto::CRYPTO_RESULT_OK) {
        error = std::format("kv store is unreadable (crypto status {}); original file was preserved", sc<uint32_t>(blob.result()));
        return KV_STORE_INIT_UNKNOWN_ERROR;
    }

    auto json = glz::read_json<SKvStorage>(blob.data());
    if (!json) {
        error = "kv store has invalid serialized data; original file was preserved";
        return KV_STORE_INIT_UNKNOWN_ERROR;
    }

    if (auto valid = validateStorage(*json); !valid) {
        error = std::format("kv store failed validation: {}; original file was preserved", valid.error());
        return KV_STORE_INIT_UNKNOWN_ERROR;
    }

    storage = std::move(*json);
    if (migrateLegacyPassword) {
        if (auto migrated = saveStorage(path, storage, password); !migrated) {
            error = std::format("loaded legacy kv store but failed to migrate it: {}", migrated.error());
            return KV_STORE_INIT_UNKNOWN_ERROR;
        }
        g_logger->log(LOG_DEBUG, "migrated legacy passwordless kv store to the current format");
    }

    g_logger->log(LOG_DEBUG, "loaded kv store");
    return KV_STORE_INIT_OK;
}

void CKvStore::init() {
    if (m_state == KV_STORE_STATE_INITIALIZING || m_state == KV_STORE_STATE_OPEN)
        return;

    if (m_initThread.joinable())
        m_initThread.join();

    {
        std::scoped_lock lock(m_initMutex);
        m_pendingInit.reset();
    }

    m_state = KV_STORE_STATE_INITIALIZING;
    g_logger->log(LOG_DEBUG, "kv: initializing");

    auto path    = storePath();
    m_initThread = std::jthread([this, path = std::move(path)](const std::stop_token& token) mutable {
        SInitResult result;

        if (!path) {
            result.error = path.error();
        } else if (auto prepared = prepareStoreDirectory(*path); !prepared) {
            result.error = prepared.error();
        } else {
            result.path   = std::move(*path);
            result.result = loadFromDisk(token, result.path, result.storage, result.password, result.error);
        }

        {
            std::scoped_lock lock(m_initMutex);
            m_pendingInit = std::move(result);
        }

        ssize_t written = 0;
        do {
            written = write(g_core->m_kvEventWrite.get(), "x", 1);
        } while (written < 0 && errno == EINTR);

        if (written != 1)
            g_logger->log(LOG_ERR, "kv: failed to notify main thread of initialization result: {}", std::strerror(errno));
    });
}

bool CKvStore::isOpen() const {
    return m_state == KV_STORE_STATE_OPEN;
}

bool CKvStore::isInitInProgress() const {
    return m_state == KV_STORE_STATE_INITIALIZING;
}

void CKvStore::onEvent() {
    std::optional<SInitResult> result;
    {
        std::scoped_lock lock(m_initMutex);
        if (!m_pendingInit)
            return;
        result = std::move(m_pendingInit);
        m_pendingInit.reset();
    }

    if (m_initThread.joinable())
        m_initThread.join();

    if (result->result != KV_STORE_INIT_OK) {
        m_state = KV_STORE_STATE_FAILED;
        g_logger->log(result->result == KV_STORE_INIT_CANCELLED ? LOG_DEBUG : LOG_ERR, "kv: initialization failed: {}", result->error);

        const bool retry = m_retryAfterInit;
        m_retryAfterInit = false;
        if (retry && result->result != KV_STORE_INIT_CANCELLED)
            init();
        return;
    }

    m_retryAfterInit = false;

    m_storage   = std::move(result->storage);
    m_password  = std::move(result->password);
    m_storePath = std::move(result->path);
    m_state     = KV_STORE_STATE_OPEN;

    g_logger->log(LOG_DEBUG, "kv: store is open");
    g_core->sendKvOpen();
}

void CKvStore::onEnvUpdate() {
    if (m_state == KV_STORE_STATE_OPEN)
        return;
    if (m_state == KV_STORE_STATE_INITIALIZING) {
        m_retryAfterInit = true;
        return;
    }

    g_logger->log(LOG_DEBUG, "kv: environment updated, retrying store initialization");
    init();
}

std::expected<void, std::string> CKvStore::saveToDisk() {
    if (m_storePath.empty())
        return std::unexpected("store path is unavailable");

    return saveStorage(m_storePath, m_storage, m_password);
}

std::expected<void, std::string> CKvStore::setGlobal(const std::string_view& key, const std::string_view& val) {
    if (auto valid = validateKey(key); !valid)
        return valid;
    if (val.size() > MAX_VALUE_SIZE)
        return std::unexpected("value is too large");

    auto previous = m_storage;
    auto entry    = std::ranges::find_if(m_storage.global, [key](const auto& candidate) { return candidate.key == key; });
    if (entry == m_storage.global.end())
        m_storage.global.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});
    else
        entry->value = val;

    if (auto saved = saveToDisk(); !saved) {
        m_storage = std::move(previous);
        return saved;
    }

    return {};
}

std::expected<void, std::string> CKvStore::setTavern(const std::string_view& key, const std::string_view& val) {
    if (auto valid = validateKey(key); !valid)
        return valid;
    if (val.size() > MAX_VALUE_SIZE)
        return std::unexpected("value is too large");

    auto previous = m_storage;
    auto entry    = std::ranges::find_if(m_storage.tavern, [key](const auto& candidate) { return candidate.key == key; });
    if (entry == m_storage.tavern.end())
        m_storage.tavern.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});
    else
        entry->value = val;

    if (auto saved = saveToDisk(); !saved) {
        m_storage = std::move(previous);
        return saved;
    }

    return {};
}

std::expected<void, std::string> CKvStore::setApp(const std::string_view& app, const std::string_view& key, const std::string_view& val) {
    if (app.empty() || app.size() > MAX_APP_IDENTIFIER_SIZE || app.find('\0') != std::string_view::npos)
        return std::unexpected("application identifier is invalid");
    if (auto valid = validateKey(key); !valid)
        return valid;
    if (val.size() > MAX_VALUE_SIZE)
        return std::unexpected("value is too large");

    auto previous = m_storage;
    auto appIt    = std::ranges::find_if(m_storage.apps, [app](const auto& candidate) { return candidate.appName == app; });
    if (appIt == m_storage.apps.end()) {
        m_storage.apps.emplace_back(SKvApp{
            .appName = std::string{app},
            .entries = {SKvEntry{.key = std::string{key}, .value = std::string{val}}},
        });
    } else {
        auto entry = std::ranges::find_if(appIt->entries, [key](const auto& candidate) { return candidate.key == key; });
        if (entry == appIt->entries.end())
            appIt->entries.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});
        else
            entry->value = val;
    }

    if (auto saved = saveToDisk(); !saved) {
        m_storage = std::move(previous);
        return saved;
    }

    return {};
}

std::optional<std::string> CKvStore::getGlobal(const std::string_view& key) const {
    const auto entry = std::ranges::find_if(m_storage.global, [key](const auto& candidate) { return candidate.key == key; });
    return entry == m_storage.global.end() ? std::nullopt : std::optional<std::string>{entry->value};
}

std::optional<std::string> CKvStore::getTavern(const std::string_view& key) const {
    const auto entry = std::ranges::find_if(m_storage.tavern, [key](const auto& candidate) { return candidate.key == key; });
    return entry == m_storage.tavern.end() ? std::nullopt : std::optional<std::string>{entry->value};
}

std::optional<std::string> CKvStore::getApp(const std::string_view& app, const std::string_view& key) const {
    const auto appIt = std::ranges::find_if(m_storage.apps, [app](const auto& candidate) { return candidate.appName == app; });
    if (appIt == m_storage.apps.end())
        return std::nullopt;

    const auto entry = std::ranges::find_if(appIt->entries, [key](const auto& candidate) { return candidate.key == key; });
    return entry == appIt->entries.end() ? std::nullopt : std::optional<std::string>{entry->value};
}
