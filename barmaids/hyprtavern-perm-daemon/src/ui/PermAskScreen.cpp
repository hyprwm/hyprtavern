#include "GUI.hpp"
#include "../helpers/Logger.hpp"

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Null.hpp>

#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/memory/UniquePtr.hpp>
#include <hyprutils/string/ConstVarList.hpp>
#include <hyprutils/string/String.hpp>
#include <hyprutils/string/VarList2.hpp>
#include <hyprutils/os/Process.hpp>

#include <xkbcommon/xkbcommon-keysyms.h>

using namespace Hyprutils::Memory;
using namespace Hyprutils::Math;
using namespace Hyprutils::String;
using namespace Hyprutils::OS;
using namespace Hyprtoolkit;
using namespace GUI;

static struct {
    SP<CTextElement>         title;
    SP<CTextElement>         text;
    SP<CColumnLayoutElement> layoutInner;
} state;

//
static std::optional<bool> run(const std::string& appName, const std::string& appID) {
    if (!backend)
        backend = IBackend::create();

    bool allowed = false;

    if (!backend) {
        g_logger->log(LOG_ERR, "toolkit: failed to open a dialog");
        return std::nullopt;
    }

    //
    const Vector2D WINDOW_SIZE = {400, 150};
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

    state.layoutInner = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.85F, 1.F}})->gap(10)->commence();

    window->m_rootElement->addChild(layout);

    layout->addChild(state.layoutInner);
    state.layoutInner->setGrow(true);

    state.title = CTextBuilder::begin()->text("")->fontSize(CFontSize{CFontSize::HT_FONT_H2})->color([] { return backend->getPalette()->m_colors.text; })->commence();

    state.text = CTextBuilder::begin()
                     ->text(std::format("An app \"{}\" -> \"{}\" is asking for a permission XDDDD", appName, appID))
                     ->fontSize(CFontSize{CFontSize::HT_FONT_TEXT})
                     ->color([] { return backend->getPalette()->m_colors.text; })
                     ->async(false)
                     ->commence();

    std::vector<SP<CButtonElement>> buttons;

    buttons.emplace_back(CButtonBuilder::begin()
                             ->label("Deny")
                             ->onMainClick([&allowed, w = WP<IWindow>{window}](auto) {
                                 allowed = false;

                                 if (w)
                                     w->close();
                                 backend->destroy();
                             })
                             ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1, 1}})
                             ->commence());

    buttons.emplace_back(CButtonBuilder::begin()
                             ->label("Allow")
                             ->onMainClick([&allowed, w = WP<IWindow>{window}](auto) {
                                 allowed = true;

                                 if (w)
                                     w->close();
                                 backend->destroy();
                             })
                             ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1, 1}})
                             ->commence());

    auto null2 = CNullBuilder::begin()->commence();

    auto layout2 = CRowLayoutBuilder::begin()->gap(3)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1, 1}})->commence();

    null2->setGrow(true);

    window->m_events.keyboardKey.listenStatic([w = WP<IWindow>{window}](Input::SKeyboardKeyEvent ev) {
        if (ev.xkbKeysym == XKB_KEY_Return || ev.xkbKeysym == XKB_KEY_Escape) {
            if (w)
                w->close();
            backend->destroy();
        }
    });

    state.layoutInner->addChild(state.title);
    state.layoutInner->addChild(state.text);

    layout2->addChild(null2);
    for (const auto& b : buttons) {
        layout2->addChild(b);
    }

    layout->addChild(layout2);

    window->m_events.closeRequest.listenStatic([w = WP<IWindow>{window}] {
        w->close();
        backend->destroy();
    });

    window->open();

    backend->enterLoop();

    return allowed;
}

std::expected<bool, std::string> GUI::permissionAsk(const std::string& appName, const std::string& appID) {
    auto RET = run(appName, appID);

    state = {};
    backend.reset();

    if (RET)
        return *RET;

    return std::unexpected("could not open a window");
}
