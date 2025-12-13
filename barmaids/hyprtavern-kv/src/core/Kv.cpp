#include "Kv.hpp"

#include "../helpers/Logger.hpp"
#include "../ui/GUI.hpp"

#include "Crypto.hpp"

#include <filesystem>

#include <hyprutils/os/File.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include <glaze/glaze.hpp>

using namespace Hyprutils::Utils;

constexpr const char* KV_STORE_FILE_NAME   = "hyprtavern-kv.dat";
constexpr const char* TAVERN_DATA_DIR_NAME = "hyprtavern";

//
std::future<bool> CKvStore::init() {

    auto        future = m_initPromise.get_future();
    std::thread t([this] {
        static const auto HOME = getenv("HOME");

        if (!HOME) {
            g_logger->log(LOG_ERR, "Can't create kv store: no $HOME");
            m_initPromise.set_value(false);
            return;
        }

        const auto      DIR_PATH = std::filesystem::path{HOME} / ".local" / "share" / TAVERN_DATA_DIR_NAME;

        std::error_code ec;

        if (!std::filesystem::exists(DIR_PATH, ec) || ec) {
            // attempt to create
            g_logger->log(LOG_DEBUG, "store dir at {} seems to not exist, creating.", DIR_PATH.string());
            std::filesystem::create_directories(DIR_PATH, ec);

            if (ec) {
                g_logger->log(LOG_ERR, "failed to create store dir at {}.", DIR_PATH.string());
                m_initPromise.set_value(false);
                return;
            }
        }

        loadFromDisk();

        m_initPromise.set_value(true);
        return;
    });

    t.detach();

    return future;
}

void CKvStore::setGlobal(const std::string_view& key, const std::string_view& val) {
    CScopeGuard x([this] { saveToDisk(); });

    for (auto& [k, v] : m_storage.global) {
        if (k != key)
            continue;

        v = val;
        return;
    }

    m_storage.global.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});
}

void CKvStore::setTavern(const std::string_view& key, const std::string_view& val) {
    CScopeGuard x([this] { saveToDisk(); });

    for (auto& [k, v] : m_storage.tavern) {
        if (k != key)
            continue;

        v = val;
        return;
    }

    m_storage.tavern.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});
}

void CKvStore::setApp(const std::string_view& app, const std::string_view& key, const std::string_view& val) {
    CScopeGuard x([this] { saveToDisk(); });

    auto        appIt = std::ranges::find_if(m_storage.apps, [&app](const auto& e) { return e.appName == app; });
    if (appIt == m_storage.apps.end()) {
        m_storage.apps.emplace_back(SKvApp{
            .appName = std::string{app},
            .entries = {SKvEntry{.key = std::string{key}, .value = std::string{val}}},
        });
        return;
    }

    for (auto& [k, v] : appIt->entries) {
        if (k != key)
            continue;

        v = val;
        return;
    }

    appIt->entries.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});
}

std::optional<std::string> CKvStore::getGlobal(const std::string_view& key) {
    for (const auto& [k, v] : m_storage.global) {
        if (k != key)
            continue;

        return v;
    }

    return std::nullopt;
}

std::optional<std::string> CKvStore::getTavern(const std::string_view& key) {
    for (const auto& [k, v] : m_storage.tavern) {
        if (k != key)
            continue;

        return v;
    }

    return std::nullopt;
}

std::optional<std::string> CKvStore::getApp(const std::string_view& app, const std::string_view& key) {
    auto appIt = std::ranges::find_if(m_storage.apps, [&app](const auto& e) { return e.appName == app; });
    if (appIt == m_storage.apps.end())
        return std::nullopt;

    for (const auto& [k, v] : appIt->entries) {
        if (k != key)
            continue;

        return v;
    }

    return std::nullopt;
}

void CKvStore::saveToDisk() {
    static const auto      HOME = getenv("HOME");
    const auto             PATH = std::filesystem::path{HOME} / ".local" / "share" / TAVERN_DATA_DIR_NAME / KV_STORE_FILE_NAME;

    Crypto::CEncryptedBlob blob(*glz::write_json(m_storage), m_password);
    auto                   ret = blob.writeToFile(PATH);

    if (!ret)
        g_logger->log(LOG_ERR, "failed to store kv data on disk");
}

bool CKvStore::loadFromDisk() {
    static const auto HOME = getenv("HOME");
    const auto        PATH = std::filesystem::path{HOME} / ".local" / "share" / TAVERN_DATA_DIR_NAME / KV_STORE_FILE_NAME;

    std::error_code   ec;

    auto              firstTimeSetup = [this] {
        g_logger->log(LOG_ERR, "kv store missing/corrupt: creating one");

        auto ret = GUI::firstTimeSetup();

        if (!ret) {
            g_logger->log(LOG_ERR, "failed to open gui??");
            return;
        }

        m_password = ret.value();

        saveToDisk();
    };

    if (!std::filesystem::exists(PATH, ec) || ec) {
        firstTimeSetup();
        return true;
    }

    // ask for password if necessary. Try our default one first for no-pass logins

    Crypto::CEncryptedBlob blob(PATH, m_password);

    while (blob.result() == Crypto::CRYPTO_RESULT_BAD_PW) {
        auto ret = GUI::passwordAsk();

        if (!ret) {
            g_logger->log(LOG_DEBUG, "kv store: passwordAsk bad ret {}", ret.error());
            break;
        }

        m_password = ret.value();

        blob = Crypto::CEncryptedBlob(PATH, m_password);

        if (blob.result() == Crypto::CRYPTO_RESULT_BAD_PW)
            continue;

        g_logger->log(LOG_DEBUG, "kv store: break on status {}", sc<uint32_t>(blob.result()));

        break;
    }

    if (blob.result() != Crypto::CRYPTO_RESULT_OK) {
        g_logger->log(LOG_ERR, "kv store corrupt: bad content, status {}, recreating one", sc<uint32_t>(blob.result()));
        firstTimeSetup();
        return true;
    }

    auto json = glz::read_json<SKvStorage>(blob.data());

    if (!json) {
        g_logger->log(LOG_ERR, "kv store corrupt: bad content, recreating one.");
        firstTimeSetup();
        return true;
    }

    m_storage = *json;

    g_logger->log(LOG_DEBUG, "loaded kv store");
    return true;
}
