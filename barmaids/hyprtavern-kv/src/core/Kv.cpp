#include "Kv.hpp"

#include "../helpers/Logger.hpp"
#include "../ui/SetupScreen.hpp"

#include <fstream>
#include <filesystem>

#include <hyprutils/os/File.hpp>

#include <glaze/glaze.hpp>

constexpr const char* KV_STORE_FILE_NAME   = "hyprtavern-kv.dat";
constexpr const char* TAVERN_DATA_DIR_NAME = "hyprtavern";

//
void CKvStore::init() {
    static const auto HOME = getenv("HOME");

    if (!HOME) {
        g_logger->log(LOG_ERR, "Can't create kv store: no $HOME");
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
            return;
        }
    }

    // load
    const auto FILE_READ = Hyprutils::File::readFileAsString((DIR_PATH / KV_STORE_FILE_NAME).string());
    if (!FILE_READ) {
        g_logger->log(LOG_ERR, "failed to read store file. Creating a new one...", DIR_PATH.string());

        auto ret = GUI::firstTimeSetup();

        g_logger->log(LOG_DEBUG, "REMOVE ME: USER CHOSE {}", ret.value_or("ERRRRRR"));

        saveToDisk();
    }
}

void CKvStore::setGlobal(const std::string_view& key, const std::string_view& val) {
    for (auto& [k, v] : m_storage.global) {
        if (k != key)
            continue;

        v = val;
        return;
    }

    m_storage.global.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});

    saveToDisk();
}

void CKvStore::setTavern(const std::string_view& key, const std::string_view& val) {
    for (auto& [k, v] : m_storage.tavern) {
        if (k != key)
            continue;

        v = val;
        return;
    }

    m_storage.tavern.emplace_back(SKvEntry{.key = std::string{key}, .value = std::string{val}});

    saveToDisk();
}

void CKvStore::setApp(const std::string_view& app, const std::string_view& key, const std::string_view& val) {
    auto appIt = std::ranges::find_if(m_storage.apps, [&app](const auto& e) { return e.appName == app; });
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

    saveToDisk();
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
    static const auto HOME = getenv("HOME");

    if (!HOME) {
        g_logger->log(LOG_ERR, "Can't save kv store: no $HOME");
        return;
    }

    const auto    FILE_PATH = std::filesystem::path{HOME} / ".local" / "share" / TAVERN_DATA_DIR_NAME / KV_STORE_FILE_NAME;
    std::ofstream ofs(FILE_PATH, std::ios::trunc);
    if (!ofs.good()) {
        g_logger->log(LOG_ERR, "Can't save kv store: couldn't open file for write");
        return;
    }

    // FIXME: this needs to be enc'd

    auto str = glz::write_json(m_storage);
    if (!str) {
        g_logger->log(LOG_ERR, "Can't save kv store: failed to generate json");
        return;
    }

    ofs << *str;

    ofs.close();
}
