#include "GUI.hpp"
#include "../helpers/Logger.hpp"

#include <hp_hyprtavern_core_v1-client.hpp>

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Null.hpp>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <format>
#include <optional>
#include <vector>

using namespace Hyprutils::Memory;
using namespace Hyprutils::Math;
using namespace Hyprtoolkit;
using namespace GUI;

static struct {
    SP<CTextElement>         title;
    SP<CTextElement>         text;
    SP<CColumnLayoutElement> layoutInner;
} state;

struct SPermissionDescription {
    const char* label;
    const char* consequence;
};

static std::optional<SPermissionDescription> permissionDescription(uint32_t permission) {
    switch (permission) {
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS:
            return SPermissionDescription{"Change all system settings", "This app could change basic and sensitive system settings."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_BASIC:
            return SPermissionDescription{"Change basic system settings", "This app could change non-sensitive system settings, including your wallpaper."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_WALLPAPER:
            return SPermissionDescription{"Change your wallpaper", "This app could replace or reconfigure your desktop wallpaper."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_SENSITIVE:
            return SPermissionDescription{"Change sensitive system settings", "This app could manage secrets and permissions granted to other apps."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_MANAGE_SECRETS:
            return SPermissionDescription{"Manage encrypted secrets", "This app could read, add, change, or remove secrets managed by the session."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_SETTINGS_MANAGE_PERMISSIONS:
            return SPermissionDescription{"Manage application permissions", "This app could change permissions granted to other applications."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING:
            return SPermissionDescription{"Monitor system status", "This app could monitor all status information exposed by the session bus."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING_BASIC:
            return SPermissionDescription{"Monitor basic system status", "This app could monitor basic operations exposed by the session bus."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MONITORING_ALL_BUS_OBJECTS:
            return SPermissionDescription{"See all session bus objects", "This app could discover bus objects even when their normal permissions would hide them."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MANAGEMENT:
            return SPermissionDescription{"Manage the Hyprtavern session", "This app could perform privileged management operations for the session bus."};
        case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_MANAGEMENT_ENVIRONMENT:
            return SPermissionDescription{"Change the session environment", "This app could add, change, or remove environment variables used by the session and its services."};
        default: return std::nullopt;
    }
}

static std::optional<ePermissionChoice> run(const std::string& appName, const std::string& appID, const SPermissionDescription& description,
                                            hpHyprtavernPermissionAuthenticationV1AskType askType) {
    if (!backend)
        backend = IBackend::create();

    ePermissionChoice choice = PERMISSION_CHOICE_DENY;

    if (!backend) {
        g_logger->log(LOG_ERR, "toolkit: failed to open a permission dialog");
        return std::nullopt;
    }

    const Vector2D WINDOW_SIZE = {560, 300};
    auto           window      = CWindowBuilder::begin()
                                     ->preferredSize(WINDOW_SIZE)
                                     ->minSize(WINDOW_SIZE)
                                     ->maxSize(WINDOW_SIZE)
                                     ->appTitle("Hyprtavern Permission Prompt")
                                     ->appClass("hyprtavern-perm-daemon")
                                     ->commence();

    window->m_rootElement->addChild(CRectangleBuilder::begin()->color([] { return backend->getPalette()->m_colors.background; })->commence());

    auto layout = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->commence();
    layout->setMargin(3);

    state.layoutInner = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.9F, 1.F}})->gap(10)->commence();

    window->m_rootElement->addChild(layout);
    layout->addChild(state.layoutInner);
    state.layoutInner->setGrow(true);

    state.title =
        CTextBuilder::begin()->text(description.label)->fontSize(CFontSize{CFontSize::HT_FONT_H2})->color([] { return backend->getPalette()->m_colors.text; })->commence();

    const auto REQUEST_TYPE = askType == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_ASK_TYPE_PERSIST ? "The app requested a persistent grant. You can still allow it only once." :
                                                                                                       "The app requested a one-time grant.";

    state.text = CTextBuilder::begin()
                     ->text(std::format("{} ({}) is requesting this permission.\n\nConsequence: {}\n\n{}", appName, appID, description.consequence, REQUEST_TYPE))
                     ->fontSize(CFontSize{CFontSize::HT_FONT_TEXT})
                     ->color([] { return backend->getPalette()->m_colors.text; })
                     ->async(false)
                     ->commence();

    std::vector<SP<CButtonElement>> buttons;

    buttons.emplace_back(CButtonBuilder::begin()
                             ->label("Deny")
                             ->onMainClick([&choice, w = WP<IWindow>{window}](auto) {
                                 choice = PERMISSION_CHOICE_DENY;
                                 if (w)
                                     w->close();
                                 backend->destroy();
                             })
                             ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1, 1}})
                             ->commence());

    buttons.emplace_back(CButtonBuilder::begin()
                             ->label("Allow once")
                             ->onMainClick([&choice, w = WP<IWindow>{window}](auto) {
                                 choice = PERMISSION_CHOICE_ALLOW_ONCE;
                                 if (w)
                                     w->close();
                                 backend->destroy();
                             })
                             ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1, 1}})
                             ->commence());

    if (askType == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_ASK_TYPE_PERSIST) {
        buttons.emplace_back(CButtonBuilder::begin()
                                 ->label("Always allow")
                                 ->onMainClick([&choice, w = WP<IWindow>{window}](auto) {
                                     choice = PERMISSION_CHOICE_ALLOW_ALWAYS;
                                     if (w)
                                         w->close();
                                     backend->destroy();
                                 })
                                 ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1, 1}})
                                 ->commence());
    }

    auto spacer       = CNullBuilder::begin()->commence();
    auto buttonLayout = CRowLayoutBuilder::begin()->gap(3)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1, 1}})->commence();

    spacer->setGrow(true);

    window->m_events.keyboardKey.listenStatic([w = WP<IWindow>{window}](Input::SKeyboardKeyEvent ev) {
        if (ev.xkbKeysym != XKB_KEY_Escape)
            return;

        if (w)
            w->close();
        backend->destroy();
    });

    state.layoutInner->addChild(state.title);
    state.layoutInner->addChild(state.text);

    buttonLayout->addChild(spacer);
    for (const auto& button : buttons) {
        buttonLayout->addChild(button);
    }

    layout->addChild(buttonLayout);

    window->m_events.closeRequest.listenStatic([w = WP<IWindow>{window}] {
        if (w)
            w->close();
        backend->destroy();
    });

    window->open();
    backend->enterLoop();

    return choice;
}

std::expected<ePermissionChoice, std::string> GUI::permissionAsk(const std::string& appName, const std::string& appID, uint32_t permission,
                                                                 hpHyprtavernPermissionAuthenticationV1AskType askType) {
    const auto DESCRIPTION = permissionDescription(permission);

    if (!DESCRIPTION) {
        g_logger->log(LOG_WARN, "denying unknown or internal permission id {}", permission);
        return PERMISSION_CHOICE_DENY;
    }

    if (askType != HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_ASK_TYPE_SESSION && askType != HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_ASK_TYPE_PERSIST) {
        g_logger->log(LOG_WARN, "denying permission {} with unknown ask type {}", permission, static_cast<uint32_t>(askType));
        return PERMISSION_CHOICE_DENY;
    }

    auto result = run(appName, appID, *DESCRIPTION, askType);

    state = {};
    backend.reset();

    if (result)
        return *result;

    return std::unexpected("could not open a window");
}
