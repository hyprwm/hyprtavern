#include "NaiveIdentity.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>

#include <climits>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/stat.h>
#endif

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/param.h>
#include <sys/sysctl.h>
#if defined(__DragonFly__)
#include <sys/user.h>
#include <sys/kinfo.h>
#elif defined(__FreeBSD__)
#include <sys/user.h>
#endif
#endif

using namespace Security;

namespace {
    struct SProcessCredentials {
        std::optional<uid_t> effectiveUid;
    };

    std::string trim(std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.remove_suffix(1);
        return std::string{value};
    }

    SProcessCredentials processCredentials(pid_t pid) {
        if (pid <= 0)
            return {};

#if defined(__linux__)
        std::ifstream ifs(std::format("/proc/{}/status", pid));
        if (!ifs.good())
            return {};

        SProcessCredentials credentials;
        std::string         line;
        while (std::getline(ifs, line)) {
            unsigned int real = 0, effective = 0;
            if (line.starts_with("Uid:") && std::sscanf(line.c_str(), "Uid:\t%u\t%u", &real, &effective) == 2)
                credentials.effectiveUid = static_cast<uid_t>(effective);
        }
        return credentials;
#elif defined(__FreeBSD__)
        int               mib[]   = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
        struct kinfo_proc process = {};
        size_t            size    = sizeof(process);
        if (sysctl(mib, 4, &process, &size, nullptr, 0) == 0 && size >= sizeof(process))
            return {.effectiveUid = process.ki_uid};
#elif defined(__DragonFly__)
        int               mib[]   = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
        struct kinfo_proc process = {};
        size_t            size    = sizeof(process);
        if (sysctl(mib, 4, &process, &size, nullptr, 0) == 0 && size >= sizeof(process))
            return {.effectiveUid = process.kp_uid};
#elif defined(__OpenBSD__)
        int               mib[]   = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid, sizeof(struct kinfo_proc), 1};
        struct kinfo_proc process = {};
        size_t            size    = sizeof(process);
        if (sysctl(mib, 6, &process, &size, nullptr, 0) == 0 && size >= sizeof(process))
            return {.effectiveUid = process.p_uid};
#elif defined(__NetBSD__)
        int                mib[]   = {CTL_KERN, KERN_PROC2, KERN_PROC_PID, pid, sizeof(struct kinfo_proc2), 1};
        struct kinfo_proc2 process = {};
        size_t             size    = sizeof(process);
        if (sysctl(mib, 6, &process, &size, nullptr, 0) == 0 && size >= sizeof(process))
            return {.effectiveUid = process.p_uid};
#endif

        return {};
    }

    std::string executablePathForPid(pid_t pid) {
        if (pid <= 0)
            return "";

#if defined(__linux__)
        std::array<char, PATH_MAX + 1> path = {};
        const auto                     proc = std::format("/proc/{}/exe", pid);
        const ssize_t                  len  = readlink(proc.c_str(), path.data(), path.size() - 1);
        if (len <= 0)
            return "";

        path[static_cast<size_t>(len)] = '\0';
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
        const unsigned int miblen        = sizeof(mib) / sizeof(mib[0]);
        char               exe[PATH_MAX] = {};
        size_t             size          = sizeof(exe);
        if (sysctl(mib, miblen, &exe, &size, nullptr, 0) < 0 || size == 0)
            return "";

        return exe;
#else
        return "";
#endif
    }

    std::pair<bool, bool> detectChroot(pid_t pid) {
#if defined(__linux__)
        if (pid <= 0)
            return {false, false};

        struct stat hostRoot = {};
        struct stat procRoot = {};
        if (stat("/", &hostRoot) < 0)
            return {false, false};

        const auto procRootPath = std::format("/proc/{}/root", pid);
        if (stat(procRootPath.c_str(), &procRoot) < 0)
            return {false, false};

        return {true, hostRoot.st_dev != procRoot.st_dev || hostRoot.st_ino != procRoot.st_ino};
#else
        (void)pid;
        return {false, false};
#endif
    }

    struct SFlatpakIdentity {
        bool                       detected = false;
        std::optional<std::string> appID;
    };

    SFlatpakIdentity flatpakIdentity(pid_t pid) {
#if defined(__linux__)
        if (pid <= 0)
            return {};

        std::ifstream info(std::format("/proc/{}/root/.flatpak-info", pid));
        if (!info.good())
            return {};

        SFlatpakIdentity identity{.detected = true};

        bool             inApplicationSection = false;
        std::string      line;
        while (std::getline(info, line)) {
            const auto cleaned = trim(line);
            if (cleaned.empty() || cleaned.starts_with('#') || cleaned.starts_with(';'))
                continue;

            if (cleaned.front() == '[' && cleaned.back() == ']') {
                inApplicationSection = std::string_view{cleaned}.substr(1, cleaned.size() - 2) == "Application";
                continue;
            }

            if (!inApplicationSection)
                continue;

            const auto separator = cleaned.find('=');
            if (separator == std::string::npos || trim(std::string_view{cleaned}.substr(0, separator)) != "name")
                continue;

            auto id = trim(std::string_view{cleaned}.substr(separator + 1));
            if (!id.empty())
                identity.appID = std::move(id);
            return identity;
        }
        return identity;
#else
        (void)pid;
        return {};
#endif
    }
}

CNaiveSecurityIdentityProvider::CNaiveSecurityIdentityProvider(SP<Hyprwire::IServerClient> client, ePrincipalAuthority authority) {
    if (!client) {
        m_identity = "unknown:pid=-1";
        return;
    }

    const pid_t PID                     = client->getPID();
    const auto  CREDENTIALS             = processCredentials(PID);
    const bool  SAME_EUID               = CREDENTIALS.effectiveUid && *CREDENTIALS.effectiveUid == geteuid();
    const auto [CHROOT_KNOWN, CHROOTED] = detectChroot(PID);
    const auto EXECUTABLE               = executablePathForPid(PID);
    const auto FLATPAK                  = flatpakIdentity(PID);

    m_pid = PID;
    if (!EXECUTABLE.empty()) {
        m_path              = EXECUTABLE;
        const auto filename = std::filesystem::path(EXECUTABLE).filename().string();
        m_displayName       = filename.empty() ? EXECUTABLE : filename;
    }

    if (authority == ePrincipalAuthority::INTERNAL) {
        m_classification  = eIdentityClass::INTERNAL;
        m_trustworthy     = PID > 0;
        m_appID           = "hyprtavern";
        m_appIDPersistent = true;
        m_identity        = std::format("internal:pid={}", PID);
        return;
    }

    if (FLATPAK.detected) {
        m_classification = eIdentityClass::SANDBOXED;
        m_trustworthy    = PID > 0 && SAME_EUID;
        if (FLATPAK.appID) {
            m_appID           = std::format("flatpak:{}", *FLATPAK.appID);
            m_displayName     = *FLATPAK.appID;
            m_appIDPersistent = true;
        }
        m_identity = std::format("flatpak:name={}:euid={}", FLATPAK.appID.value_or("unknown"), CREDENTIALS.effectiveUid.value_or(static_cast<uid_t>(-1)));
        return;
    }

#if defined(__linux__)
    const bool VERIFIED_HOST = SAME_EUID && CHROOT_KNOWN && !CHROOTED;
#else
    // Until a platform-specific jail/confinement detector is available, a BSD peer is
    // ambiguous and must not receive the host permission bypass.
    const bool VERIFIED_HOST = false;
#endif
    if (VERIFIED_HOST) {
        m_classification  = eIdentityClass::HOST_SAME_UID;
        m_trustworthy     = PID > 0;
        m_appID           = EXECUTABLE.empty() ? std::nullopt : std::optional<std::string>{EXECUTABLE};
        m_appIDPersistent = !EXECUTABLE.empty();
        m_identity        = std::format("host:path={}:euid={}", EXECUTABLE.empty() ? "??" : EXECUTABLE, *CREDENTIALS.effectiveUid);
        return;
    }

    if (CHROOT_KNOWN && CHROOTED) {
        m_classification = eIdentityClass::SANDBOXED;
        m_appID          = EXECUTABLE.empty() ? std::nullopt : std::optional<std::string>{EXECUTABLE};
        m_identity       = std::format("sandbox:path={}:euid={}", EXECUTABLE.empty() ? "??" : EXECUTABLE, CREDENTIALS.effectiveUid.value_or(static_cast<uid_t>(-1)));
        return;
    }

    m_classification = eIdentityClass::UNKNOWN;
    m_identity       = std::format("unknown:path={}:pid={}:euid={}", EXECUTABLE.empty() ? "??" : EXECUTABLE, PID, CREDENTIALS.effectiveUid.value_or(static_cast<uid_t>(-1)));
}

eProviderType CNaiveSecurityIdentityProvider::type() const {
    return PROVIDER_NAIVE;
}

eIdentityClass CNaiveSecurityIdentityProvider::classification() const {
    return m_classification;
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

bool CNaiveSecurityIdentityProvider::appIDPersistent() const {
    return m_appIDPersistent;
}

const std::string& CNaiveSecurityIdentityProvider::identity() const {
    return m_identity;
}

int CNaiveSecurityIdentityProvider::pid() const {
    return m_pid;
}
