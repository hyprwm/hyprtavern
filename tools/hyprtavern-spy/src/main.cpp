#include <hyprtavern-client/hyprtavern-client.hpp>

#include <hyprutils/memory/Casts.hpp>

#include <format>
#include <print>
#include <ranges>

using namespace Hyprtavern;
using namespace Hyprutils::Memory;

constexpr const char* MONITORING_PERMISSION = "core.monitoring.sensitive.all_bus_objects";

static std::string    requirements(const std::vector<std::string>& permissions) {
    if (permissions.empty())
        return "nothing";

    return permissions | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>();
}

int main() {
    const auto TAVERN = ITavern::open({.appName = "hyprtavern-spy", .appDescription = "Hyprtavern spy utility"});
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
        const auto PROVIDER = object.provider.id.empty() ? std::string{"unknown"} : std::format("{} (kind {})", object.provider.id, sc<int>(object.provider.kind));

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
