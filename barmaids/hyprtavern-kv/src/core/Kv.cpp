#include "Kv.hpp"
#include "Core.hpp"

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
void CKvStore::init() {
    m_initPromise = {};
    m_initFuture  = m_initPromise.get_future();

    g_logger->log(LOG_DEBUG, "kv: initializing");

    std::thread t([this] {
        static const auto HOME = getenv("HOME");

        if (!HOME) {
            g_logger->log(LOG_ERR, "Can't create kv store: no $HOME");
            m_initPromise.set_value(KV_STORE_INIT_CANT_SHOW);
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
                m_initPromise.set_value(KV_STORE_INIT_UNKNOWN_ERROR);
                return;
            }
        }

        auto ret = loadFromDisk();
        m_initPromise.set_value(ret);
        if (ret == KV_STORE_INIT_OK)
            m_open = true;

        g_logger->log(LOG_DEBUG, "kv: init result {}", sc<uint32_t>(ret));

        // wake up the main thread
        write(g_core->m_kvEventWrite.get(), "x", 1);
        return;
    });

    t.detach();
}

bool CKvStore::isOpen() {
    return m_open;
}

bool CKvStore::isInitInProgress() {
    return m_initFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void CKvStore::onEvent() {
    if (m_open) {
        // wee, we opened, send that we are open to everyone.
        g_core->sendKvOpen();
        return;
    }

    // we did not open, oh noes...
    // something will pick this up later, like an env update.
}

void CKvStore::onEnvUpdate() {
    if (m_open)
        return;

    g_logger->log(LOG_DEBUG, "kv: env updated, let's retry opening store");

    init();
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

CKvStore::eKvStoreInitResult CKvStore::loadFromDisk() {
    static const auto HOME = getenv("HOME");
    const auto        PATH = std::filesystem::path{HOME} / ".local" / "share" / TAVERN_DATA_DIR_NAME / KV_STORE_FILE_NAME;

    std::error_code   ec;

    auto              firstTimeSetup = [this] -> eKvStoreInitResult {
        g_logger->log(LOG_ERR, "kv store missing/corrupt: creating one");

        auto ret = GUI::firstTimeSetup();

        if (!ret)
            return KV_STORE_INIT_CANT_SHOW;

        m_password = ret.value();

        saveToDisk();

        return KV_STORE_INIT_OK;
    };

    if (!std::filesystem::exists(PATH, ec) || ec)
        return firstTimeSetup();

    // ask for password if necessary. Try our default one first for no-pass logins

    Crypto::CEncryptedBlob blob(PATH, m_password);

    while (blob.result() == Crypto::CRYPTO_RESULT_BAD_PW) {
        auto ret = GUI::passwordAsk();

        if (!ret)
            return KV_STORE_INIT_CANT_SHOW;

        m_password = ret.value();

        blob = Crypto::CEncryptedBlob(PATH, m_password);

        if (blob.result() == Crypto::CRYPTO_RESULT_BAD_PW)
            continue;

        g_logger->log(LOG_DEBUG, "kv store: break on status {}", sc<uint32_t>(blob.result()));

        break;
    }

    if (blob.result() != Crypto::CRYPTO_RESULT_OK) {
        g_logger->log(LOG_ERR, "kv store corrupt: bad content, status {}, recreating one", sc<uint32_t>(blob.result()));
        return firstTimeSetup();
    }

    auto json = glz::read_json<SKvStorage>(blob.data());

    if (!json) {
        g_logger->log(LOG_ERR, "kv store corrupt: bad content, recreating one.");
        return firstTimeSetup();
    }

    m_storage = *json;

    g_logger->log(LOG_DEBUG, "loaded kv store");
    return KV_STORE_INIT_OK;
}
