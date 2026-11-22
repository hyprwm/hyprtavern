#include <store/Store.hpp>

#include <gtest/gtest.h>

#include <format>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

using namespace Kv;

namespace {
    class KvStore : public ::testing::Test {
      protected:
        void SetUp() override {
            m_dir  = std::filesystem::temp_directory_path() / std::format("hyprtavern-kv-{}", getpid());
            m_path = m_dir / "store" / "kv.json";
        }

        void TearDown() override {
            std::filesystem::permissions(m_dir / "store", std::filesystem::perms::owner_all);
            std::filesystem::remove_all(m_dir);
        }

        UP<CStore> open() {
            auto store = CStore::open(m_path);
            EXPECT_TRUE(store) << store.error();
            return std::move(*store);
        }

        std::filesystem::path m_dir, m_path;
    };
}

TEST(KvKeys, ValidatesKeys) {
    EXPECT_TRUE(isValidKey("myapp:theme/dark.v2,x-y+z=w?"));
    EXPECT_FALSE(isValidKey(""));
    EXPECT_FALSE(isValidKey("with space"));
    EXPECT_FALSE(isValidKey("emoji\xF0\x9F\x8D\xBA"));
    EXPECT_FALSE(isValidKey(std::string(MAX_KEY_LENGTH + 1, 'a')));
}

TEST_F(KvStore, KeepsScopesIndependent) {
    auto store = open();

    ASSERT_TRUE(store->set("app:flatpak:org.example.A", "token", "a"));
    ASSERT_TRUE(store->set("app:flatpak:org.example.B", "token", "b"));

    EXPECT_EQ(store->get("app:flatpak:org.example.A", "token"), "a");
    EXPECT_EQ(store->get("app:flatpak:org.example.B", "token"), "b");
    EXPECT_EQ(store->get("unbounded", "token"), std::nullopt);
}

TEST_F(KvStore, PersistsAndDeletes) {
    {
        auto store = open();
        ASSERT_TRUE(store->set("tavern", "kept", "value"));
        ASSERT_TRUE(store->set("tavern", "deleted", "value"));
        ASSERT_TRUE(store->set("tavern", "deleted", ""));
        ASSERT_TRUE(store->set("tavern", "never-existed", ""));
    }

    auto store = open();
    EXPECT_EQ(store->get("tavern", "kept"), "value");
    EXPECT_EQ(store->get("tavern", "deleted"), std::nullopt);

    struct stat info = {};
    ASSERT_EQ(stat(m_path.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0600);

    ASSERT_EQ(stat(m_path.parent_path().c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0700);
}

TEST_F(KvStore, RollsBackFailedWrites) {
    auto store = open();
    ASSERT_TRUE(store->set("tavern", "key", "old"));

    // a read-only directory makes the commit fail
    std::filesystem::permissions(m_path.parent_path(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);

    EXPECT_FALSE(store->set("tavern", "key", "new"));
    EXPECT_FALSE(store->set("tavern", "other", "new"));
    EXPECT_EQ(store->get("tavern", "key"), "old");
    EXPECT_EQ(store->get("tavern", "other"), std::nullopt);
}

TEST_F(KvStore, RefusesMalformedStores) {
    std::filesystem::create_directories(m_path.parent_path());
    std::ofstream(m_path) << "{ not json";

    EXPECT_FALSE(CStore::open(m_path));
}
