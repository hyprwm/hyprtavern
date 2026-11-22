#include "Identity.hpp"

#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

using namespace Security;

namespace {
    std::string_view trim(std::string_view sv) {
        const auto BEGIN = sv.find_first_not_of(" \t\r");
        if (BEGIN == std::string_view::npos)
            return {};

        const auto END = sv.find_last_not_of(" \t\r");
        return sv.substr(BEGIN, END - BEGIN + 1);
    }

    bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
        struct stat statA = {}, statB = {};

        if (stat(a.c_str(), &statA) < 0 || stat(b.c_str(), &statB) < 0)
            return false;

        return statA.st_dev == statB.st_dev && statA.st_ino == statB.st_ino;
    }
}

bool SAppIdentity::persistable() const {
    return kind != eIdentityKind::UNKNOWN && stable && !id.empty();
}

std::optional<std::string> Security::parseFlatpakAppID(std::string_view flatpakInfo) {
    bool inApplication = false;

    while (!flatpakInfo.empty()) {
        const auto NEWLINE = flatpakInfo.find('\n');
        const auto LINE    = trim(flatpakInfo.substr(0, NEWLINE));
        flatpakInfo.remove_prefix(NEWLINE == std::string_view::npos ? flatpakInfo.size() : NEWLINE + 1);

        if (LINE.empty() || LINE.starts_with('#') || LINE.starts_with(';'))
            continue;

        if (LINE.starts_with('[') && LINE.ends_with(']')) {
            inApplication = LINE == "[Application]";
            continue;
        }

        const auto EQUALS = LINE.find('=');
        if (!inApplication || EQUALS == std::string_view::npos || trim(LINE.substr(0, EQUALS)) != "name")
            continue;

        const auto APP_ID = trim(LINE.substr(EQUALS + 1));
        if (APP_ID.empty())
            return std::nullopt;

        return std::string{APP_ID};
    }

    return std::nullopt;
}

SAppIdentity Security::probeProcess(pid_t pid, uid_t uid, const std::filesystem::path& procfs) {
#if defined(__linux__)
    if (pid <= 0 || uid != geteuid())
        return {};

    const auto    PROCESS = procfs / std::to_string(pid);

    std::ifstream flatpakInfo(PROCESS / "root" / ".flatpak-info");
    if (flatpakInfo.good()) {
        std::stringstream ss;
        ss << flatpakInfo.rdbuf();

        const auto APP_ID = parseFlatpakAppID(ss.str());
        if (!APP_ID)
            return {};

        return {.kind = eIdentityKind::FLATPAK, .id = *APP_ID, .stable = true};
    }

    // any other confinement has a different root, and cannot be attested
    if (!sameFile("/", PROCESS / "root"))
        return {};

    std::error_code ec;
    const auto      EXECUTABLE = std::filesystem::read_symlink(PROCESS / "exe", ec);
    if (ec || EXECUTABLE.empty())
        return {};

    return {.kind = eIdentityKind::HOST_EXECUTABLE, .id = EXECUTABLE.string(), .stable = true};
#else
    // no confinement detection on other platforms yet, treat everything conservatively
    return {};
#endif
}
