#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <future>

class CKvStore {
  public:
    CKvStore()  = default;
    ~CKvStore() = default;

    CKvStore(const CKvStore&) = delete;
    CKvStore(CKvStore&)       = delete;
    CKvStore(CKvStore&&)      = delete;

    enum eKvStoreInitResult : uint8_t {
        KV_STORE_INIT_UNKNOWN_ERROR = 0,
        KV_STORE_INIT_OK,
        KV_STORE_INIT_CANT_SHOW,
    };

    // runs async
    void                       init();
    bool                       isOpen();
    bool                       isInitInProgress();

    void                       onEvent();
    void                       onEnvUpdate();

    void                       setGlobal(const std::string_view& key, const std::string_view& val);
    void                       setTavern(const std::string_view& key, const std::string_view& val);
    void                       setApp(const std::string_view& app, const std::string_view& key, const std::string_view& val);

    std::optional<std::string> getGlobal(const std::string_view& key);
    std::optional<std::string> getTavern(const std::string_view& key);
    std::optional<std::string> getApp(const std::string_view& app, const std::string_view& key);

  private:
    void               saveToDisk();
    eKvStoreInitResult loadFromDisk();

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

    std::promise<eKvStoreInitResult> m_initPromise;
    std::future<eKvStoreInitResult>  m_initFuture;

    bool                             m_open = false;

    SKvStorage                       m_storage;
    std::string                      m_password = "vaxwashere"; // default pass for no-pass kv stores
};
