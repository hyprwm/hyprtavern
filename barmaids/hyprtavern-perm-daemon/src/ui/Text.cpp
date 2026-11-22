#include "Text.hpp"

#include <common/Memory.hpp>

#include <array>
#include <format>
#include <utility>

using namespace Prompts;

namespace {
    struct SStandardPermission {
        std::string_view name;
        std::string_view title;
        std::string_view consequence;
    };

    constexpr std::array<SStandardPermission, 5> STANDARD_PERMISSIONS = {{
        {"core.settings.basic.all", "Manage basic settings", "This app could change all basic, non-sensitive settings."},
        {"core.settings.basic.wallpaper", "Manage your wallpaper", "This app could replace or reconfigure your wallpaper."},
        {"core.settings.sensitive.manage_secrets", "Manage secrets", "This app could view and modify all secrets in the key-value store."},
        {"core.settings.sensitive.manage_permissions", "Manage app permissions", "This app could view and modify the permissions granted to other apps."},
        {"core.monitoring.sensitive.all_bus_objects", "See all bus objects", "This app could see every object on the bus, even ones it cannot connect to."},
    }};

    constexpr std::string_view                   REPLACEMENT_CHARACTER = "\xEF\xBF\xBD";

    const SStandardPermission*                   standardPermission(std::string_view permission) {
        for (const auto& standard : STANDARD_PERMISSIONS) {
            if (standard.name == permission)
                return &standard;
        }

        return nullptr;
    }

    // characters U+202A to U+202E and U+2066 to U+2069 can reorder text around them
    bool isBidiOverride(std::string_view text, size_t i) {
        if (i + 2 >= text.size() || sc<unsigned char>(text[i]) != 0xE2)
            return false;

        const auto SECOND = sc<unsigned char>(text[i + 1]), THIRD = sc<unsigned char>(text[i + 2]);

        return (SECOND == 0x80 && THIRD >= 0xAA && THIRD <= 0xAE) || (SECOND == 0x81 && THIRD >= 0xA6 && THIRD <= 0xA9);
    }

    std::string describeRequester(const SRequest& request) {
        const auto  ID = Text::escapeMarkup(request.attestedID);
        std::string who;

        switch (request.kind) {
            case eIdentityKind::HOST_EXECUTABLE: who = std::format("The program <b>{}</b>", ID); break;
            case eIdentityKind::FLATPAK: who = std::format("The Flatpak app <b>{}</b>", ID); break;
            case eIdentityKind::INTERNAL: who = std::format("The internal component <b>{}</b>", ID); break;
            case eIdentityKind::UNKNOWN: who = "An <b>unverified app</b>"; break;
        }

        // self-asserted, never shown as if it were verified
        if (!request.displayName.empty())
            who += std::format(", calling itself \"{}\",", Text::escapeMarkup(request.displayName));

        return who;
    }
}

std::string Text::escapeMarkup(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        const auto C = sc<unsigned char>(text[i]);

        if (isBidiOverride(text, i)) {
            escaped += REPLACEMENT_CHARACTER;
            i += 2;
            continue;
        }

        switch (C) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default:
                if (C < 0x20 || C == 0x7F)
                    escaped += REPLACEMENT_CHARACTER;
                else
                    escaped += text[i];
        }
    }

    return escaped;
}

std::string Text::permissionTitle(std::string_view permission) {
    if (const auto STANDARD = standardPermission(permission))
        return std::string{STANDARD->title};

    return "Permission request";
}

std::string Text::promptBody(const SRequest& request) {
    std::string body = std::format("{} is requesting\n<tt>{}</tt>\n\n", describeRequester(request), escapeMarkup(request.permission));

    // vendor permissions are opaque, the name is all there is
    if (const auto STANDARD = standardPermission(request.permission))
        body += STANDARD->consequence;
    else
        body += "This permission is defined by another app, not by Hyprtavern.";

    if (request.permission.ends_with(".all")) {
        const auto PARENT = std::string_view{request.permission}.substr(0, request.permission.size() - 4);
        body += std::format("\n\nThis includes every permission under <tt>{}</tt>, including ones added in the future.", escapeMarkup(PARENT));
    }

    body += request.persist ? "\n\nYou can allow it for this session, or always." : "\n\nIf allowed, it lasts until the app closes.";

    return body;
}
