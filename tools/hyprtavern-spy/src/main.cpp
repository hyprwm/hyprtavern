#include <hyprtavern-client/hyprtavern-client.hpp>

#include <format>
#include <print>
#include <ranges>
#include <string_view>

using namespace Hyprtavern;

constexpr const char* MONITORING_PERMISSION = "core.monitoring.sensitive.all_bus_objects";

// names match application_identity_kind in hp_hyprtavern_core_v1
static std::string_view kindName(eIdentityKind kind) {
    switch (kind) {
        case eIdentityKind::HOST_EXECUTABLE: return "host_executable";
        case eIdentityKind::FLATPAK: return "flatpak";
        case eIdentityKind::INTERNAL: return "internal";
        case eIdentityKind::HOST_APPLICATION: return "host_application, self-declared";
        case eIdentityKind::UNKNOWN: break;
    }

    return "unknown";
}

static std::string requirements(const std::vector<std::string>& permissions) {
    if (permissions.empty())
        return "nothing";

    return permissions | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>();
}

int main() {
    const auto TAVERN = ITavern::open({.appName = "hyprtavern-spy", .appDescription = "Hyprtavern spy utility", .appID = "org.hyprland.hyprtavern.spy"});
    if (!TAVERN) {
        std::println("err: {}", errorString(TAVERN.error()));
        return 1;
    }

    const auto& tavern = *TAVERN;

    // without it, only objects visible to us are listed
    const auto PERMISSIONS = tavern->requestPermissionsSync({MONITORING_PERMISSION}, ePermissionMode::PERMANENT);
    if (!PERMISSIONS || PERMISSIONS->at(MONITORING_PERMISSION).result != ePermissionResult::GRANTED)
        std::println("warning: permission to monitor all objects was not granted, results may be incomplete");

    const auto OBJECTS = tavern->findSync({});
    if (!OBJECTS) {
        std::println("err: failed to query the tavern: {}", errorString(OBJECTS.error()));
        return 1;
    }

    if (OBJECTS->size() == 1)
        std::println("There is {} object in the tavern:", OBJECTS->size());
    else
        std::println("There are {} objects in the tavern:", OBJECTS->size());

    for (const auto& object : *OBJECTS) {
        const auto PROVIDER = object.provider.id.empty() ? std::string{"unknown"} : std::format("{} ({})", object.provider.id, kindName(object.provider.kind));

        std::println(" ┣╸{}#{}:", object.name, object.id);
        std::println(" ┃   ┣╸provider: {}", PROVIDER);
        std::println(" ┃   ┣╸requires: {}", requirements(object.requiredPermissions));
        std::println(" ┃   ┣╸protocols:");

        size_t i = 0;
        for (const auto& [name, protocol] : object.protocols) {
            std::println(" ┃   ┃   {}╸{}@{}, requires: {}", ++i == object.protocols.size() ? "┗" : "┣", name, protocol.version, requirements(protocol.requiredPermissions));
        }

        std::println(" ┃   ┗╸props:");

        i = 0;
        for (const auto& [name, value] : object.properties) {
            std::println(" ┃       {}╸{}={}", ++i == object.properties.size() ? "┗" : "┣", name, value);
        }
    }

    return 0;
}
