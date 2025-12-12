#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

class CKvStore {
  public:
    CKvStore()  = default;
    ~CKvStore() = default;

    CKvStore(const CKvStore&) = delete;
    CKvStore(CKvStore&)       = delete;
    CKvStore(CKvStore&&)      = delete;

    // blocks until interaction ends
    void                       init();

    void                       setGlobal(const std::string_view& key, const std::string_view& val);
    void                       setTavern(const std::string_view& key, const std::string_view& val);
    void                       setApp(const std::string_view& app, const std::string_view& key, const std::string_view& val);

    std::optional<std::string> getGlobal(const std::string_view& key);
    std::optional<std::string> getTavern(const std::string_view& key);
    std::optional<std::string> getApp(const std::string_view& app, const std::string_view& key);

  private:
    void saveToDisk();

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

    SKvStorage m_storage;
};
