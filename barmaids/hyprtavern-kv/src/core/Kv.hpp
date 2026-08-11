#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <expected>
#include <filesystem>
#include <mutex>
#include <thread>

class CKvStore {
  public:
    CKvStore() = default;
    ~CKvStore();

    CKvStore(const CKvStore&) = delete;
    CKvStore(CKvStore&)       = delete;
    CKvStore(CKvStore&&)      = delete;

    enum eKvStoreInitResult : uint8_t {
        KV_STORE_INIT_UNKNOWN_ERROR = 0,
        KV_STORE_INIT_OK,
        KV_STORE_INIT_CANCELLED,
    };

    enum eKvStoreState : uint8_t {
        KV_STORE_STATE_IDLE = 0,
        KV_STORE_STATE_INITIALIZING,
        KV_STORE_STATE_OPEN,
        KV_STORE_STATE_FAILED,
    };

    // Runs asynchronously, with completion applied by onEvent on the main thread.
    void                                    init();
    bool                                    isOpen() const;
    bool                                    isInitInProgress() const;

    void                                    onEvent();
    void                                    onEnvUpdate();

    std::expected<void, std::string>        setGlobal(const std::string_view& key, const std::string_view& val);
    std::expected<void, std::string>        setTavern(const std::string_view& key, const std::string_view& val);
    std::expected<void, std::string>        setApp(const std::string_view& app, const std::string_view& key, const std::string_view& val);

    std::optional<std::string>              getGlobal(const std::string_view& key) const;
    std::optional<std::string>              getTavern(const std::string_view& key) const;
    std::optional<std::string>              getApp(const std::string_view& app, const std::string_view& key) const;

    static std::expected<void, std::string> validateKey(const std::string_view& key);

  private:
    struct SKvEntry {
        std::string key;
        std::string value;
    };

    struct SKvApp {
        std::string           appName;
        std::vector<SKvEntry> entries;
    };

    struct SKvStorage {
        std::vector<SKvApp>   apps;
        std::vector<SKvEntry> global;
        std::vector<SKvEntry> tavern;
    };

    struct SInitResult {
        eKvStoreInitResult    result = KV_STORE_INIT_UNKNOWN_ERROR;
        SKvStorage            storage;
        std::string           password;
        std::filesystem::path path;
        std::string           error;
    };

    static std::expected<std::filesystem::path, std::string> storePath();
    static std::expected<void, std::string>                  prepareStoreDirectory(const std::filesystem::path& path);
    static std::expected<void, std::string>                  validateStorage(const SKvStorage& storage);
    static std::expected<void, std::string>                  saveStorage(const std::filesystem::path& path, const SKvStorage& storage, const std::string& password);
    static eKvStoreInitResult        loadFromDisk(const std::stop_token& token, const std::filesystem::path& path, SKvStorage& storage, std::string& password, std::string& error);

    std::expected<void, std::string> saveToDisk();

    std::jthread                     m_initThread;
    std::mutex                       m_initMutex;
    std::optional<SInitResult>       m_pendingInit;
    eKvStoreState                    m_state          = KV_STORE_STATE_IDLE;
    bool                             m_retryAfterInit = false;

    SKvStorage                       m_storage;
    std::filesystem::path            m_storePath;
    std::string                      m_password;
};
