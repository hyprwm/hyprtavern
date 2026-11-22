#include <security/Identity.hpp>

#include <gtest/gtest.h>

#include <format>
#include <fstream>

#include <unistd.h>

using namespace Security;

namespace {
    class IdentityProbe : public ::testing::Test {
      protected:
        void SetUp() override {
            m_root = std::filesystem::temp_directory_path() / std::format("hyprtavern-procfs-{}", getpid());
            std::filesystem::create_directories(m_root);
        }

        void TearDown() override {
            std::filesystem::remove_all(m_root);
        }

        std::filesystem::path addProcess(pid_t pid) {
            const auto PROCESS = m_root / std::to_string(pid);
            std::filesystem::create_directories(PROCESS);
            return PROCESS;
        }

        std::filesystem::path m_root;
    };
}

TEST(Identity, ParsesFlatpakInfo) {
    EXPECT_EQ(parseFlatpakAppID("[Application]\nname=org.example.App\nruntime=x\n"), "org.example.App");
    EXPECT_EQ(parseFlatpakAppID("# comment\n[Instance]\nname=wrong\n\n[Application]\n  name = org.example.App  \n"), "org.example.App");
    EXPECT_EQ(parseFlatpakAppID("[Instance]\nname=org.example.App\n"), std::nullopt);
    EXPECT_EQ(parseFlatpakAppID("[Application]\nname=\n"), std::nullopt);
    EXPECT_EQ(parseFlatpakAppID(""), std::nullopt);
}

TEST_F(IdentityProbe, AttestsHostExecutables) {
    const auto PROCESS = addProcess(100);
    std::filesystem::create_directory_symlink("/", PROCESS / "root");
    std::filesystem::create_symlink("/usr/bin/example", PROCESS / "exe");

    const auto IDENTITY = probeProcess(100, geteuid(), m_root);

    EXPECT_EQ(IDENTITY.kind, eIdentityKind::HOST_EXECUTABLE);
    EXPECT_EQ(IDENTITY.id, "/usr/bin/example");
    EXPECT_TRUE(IDENTITY.persistable());
}

TEST_F(IdentityProbe, AttestsFlatpakApplications) {
    const auto PROCESS = addProcess(101);
    std::filesystem::create_directories(PROCESS / "root");
    std::ofstream(PROCESS / "root" / ".flatpak-info") << "[Application]\nname=org.example.App\n";

    const auto IDENTITY = probeProcess(101, geteuid(), m_root);

    EXPECT_EQ(IDENTITY.kind, eIdentityKind::FLATPAK);
    EXPECT_EQ(IDENTITY.id, "org.example.App");
    EXPECT_TRUE(IDENTITY.persistable());
}

TEST_F(IdentityProbe, TreatsAmbiguousProcessesAsUnknown) {
    // different root without flatpak metadata
    const auto CONFINED = addProcess(102);
    std::filesystem::create_directories(CONFINED / "root");
    std::filesystem::create_symlink("/usr/bin/example", CONFINED / "exe");

    // flatpak metadata without an app id
    const auto NAMELESS = addProcess(103);
    std::filesystem::create_directories(NAMELESS / "root");
    std::ofstream(NAMELESS / "root" / ".flatpak-info") << "[Application]\n";

    // host root, but no readable executable
    const auto NO_EXE = addProcess(104);
    std::filesystem::create_directory_symlink("/", NO_EXE / "root");

    EXPECT_EQ(probeProcess(102, geteuid(), m_root), SAppIdentity{});
    EXPECT_EQ(probeProcess(103, geteuid(), m_root), SAppIdentity{});
    EXPECT_EQ(probeProcess(104, geteuid(), m_root), SAppIdentity{});
    EXPECT_EQ(probeProcess(-1, geteuid(), m_root), SAppIdentity{});
}

TEST_F(IdentityProbe, RejectsOtherUsers) {
    const auto PROCESS = addProcess(105);
    std::filesystem::create_directory_symlink("/", PROCESS / "root");
    std::filesystem::create_symlink("/usr/bin/example", PROCESS / "exe");

    EXPECT_EQ(probeProcess(105, geteuid() + 1, m_root), SAppIdentity{});
}
