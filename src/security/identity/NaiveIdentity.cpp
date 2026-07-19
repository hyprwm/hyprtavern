#include "NaiveIdentity.hpp"

#include <filesystem>
#include <array>
#include <cstdio>
#include <fstream>
#include <format>
#include <utility>
#include <climits>

#include <sys/types.h>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/param.h>
#include <sys/sysctl.h>
#if defined(__DragonFly__)
#include <sys/kinfo.h>
#elif defined(__FreeBSD__)
#include <sys/user.h>
#endif
#endif

using namespace Security;

static std::pair<int, int> readUidGidFromProcStatus(pid_t pid) {
#if defined(__linux__)
    if (pid <= 0)
        return {-1, -1};

    std::ifstream ifs(std::format("/proc/{}/status", pid));
    if (!ifs.good())
        return {-1, -1};

    int         realUid = -1;
    int         realGid = -1;

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.starts_with("Uid:")) {
            std::sscanf(line.c_str(), "Uid:\t%d", &realUid);
        } else if (line.starts_with("Gid:")) {
            std::sscanf(line.c_str(), "Gid:\t%d", &realGid);
        }
    }

    return {realUid, realGid};
#else
    (void)pid;
    return {-1, -1};
#endif
}

static std::string executablePathForPid(pid_t pid) {
    if (pid <= 0)
        return "";

#if defined(__linux__)
    std::array<char, PATH_MAX + 1> path = {};
    const auto                     PROC = std::format("/proc/{}/exe", pid);
    const ssize_t                  len  = readlink(PROC.c_str(), path.data(), path.size() - 1);
    if (len <= 0)
        return "";

    path[sc<size_t>(len)] = '\0';
    return path.data();
#elif defined(KERN_PROC_PATHNAME)
    int mib[] = {
        CTL_KERN,
#if defined(__NetBSD__)
        KERN_PROC_ARGS,
        pid,
        KERN_PROC_PATHNAME,
#else
        KERN_PROC,
        KERN_PROC_PATHNAME,
        pid,
#endif
    };
    u_int  miblen        = sizeof(mib) / sizeof(mib[0]);
    char   exe[PATH_MAX] = "/nonexistent";
    size_t sz            = sizeof(exe);
    if (sysctl(mib, miblen, &exe, &sz, NULL, 0) < 0)
        return "";

    return exe;
#else
    return "";
#endif
}

static std::pair<bool, bool> detectChroot(pid_t pid) {
#if defined(__linux__)
    if (pid <= 0)
        return {false, false};

    struct stat hostRoot = {};
    struct stat procRoot = {};

    if (stat("/", &hostRoot) < 0)
        return {false, false};

    const auto PROC_ROOT = std::format("/proc/{}/root", pid);
    if (stat(PROC_ROOT.c_str(), &procRoot) < 0)
        return {false, false};

    return {true, hostRoot.st_dev != procRoot.st_dev || hostRoot.st_ino != procRoot.st_ino};
#else
    (void)pid;
    return {false, false};
#endif
}

CNaiveSecurityIdentityProvider::CNaiveSecurityIdentityProvider(SP<Hyprwire::IServerClient> client) {
    if (!client)
        return;

    const pid_t PID                     = client->getPID();
    const auto [UID, GID]               = readUidGidFromProcStatus(PID);
    const auto [CHROOT_KNOWN, CHROOTED] = detectChroot(PID);

    const auto APP_ID = executablePathForPid(PID);

    if (!APP_ID.empty()) {
        m_appID         = APP_ID;
        const auto PATH = std::filesystem::path(APP_ID).filename().string();
        m_displayName   = PATH.empty() ? APP_ID : PATH;
        m_path          = PATH;
    }

    m_pid = PID;

    m_trustworthy = PID > 0 && m_appID.has_value() && (!CHROOTED || CHROOT_KNOWN);
    m_identity    = std::format("naive:path={}:uid={}:gid={}:chrooted={}", m_appID.value_or("??"), UID, GID, CHROOTED);
}

eProviderType CNaiveSecurityIdentityProvider::type() const {
    return PROVIDER_NAIVE;
}

bool CNaiveSecurityIdentityProvider::trustworthy() const {
    return m_trustworthy;
}

const std::optional<std::string>& CNaiveSecurityIdentityProvider::appID() const {
    return m_appID;
}

const std::optional<std::string>& CNaiveSecurityIdentityProvider::displayName() const {
    return m_displayName;
}

const std::optional<std::string>& CNaiveSecurityIdentityProvider::path() const {
    return m_path;
}

const std::string& CNaiveSecurityIdentityProvider::identity() const {
    return m_identity;
}

int CNaiveSecurityIdentityProvider::pid() const {
    return m_pid;
}
