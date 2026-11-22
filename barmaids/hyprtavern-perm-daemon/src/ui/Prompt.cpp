#include "Prompt.hpp"
#include "Text.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <string>
#include <vector>

using namespace Hyprtoolkit;
using namespace Hyprutils::Math;

SP<IWindow> UI::openPrompt(const SP<IBackend>& backend, const Prompts::SRequest& request, std::function<void(eChoice)>&& onChoice) {
    const Vector2D WINDOW_SIZE = {560, 320};

    auto           window = CWindowBuilder::begin()
                                ->preferredSize(WINDOW_SIZE)
                                ->minSize(WINDOW_SIZE)
                                ->maxSize(WINDOW_SIZE)
                                ->appTitle("Hyprtavern Permission Prompt")
                                ->appClass("hyprtavern-perm-daemon")
                                ->commence();

    // runs once, whichever way the prompt is answered
    const auto CHOOSE = [callback = makeShared<std::function<void(eChoice)>>(std::move(onChoice))](eChoice choice) {
        if (!*callback)
            return;

        auto fn   = std::move(*callback);
        *callback = nullptr;
        fn(choice);
    };

    const auto PALETTE = [weak = WP<IBackend>{backend}] { return weak->getPalette(); };

    window->m_rootElement->addChild(CRectangleBuilder::begin()->color([PALETTE] { return PALETTE()->m_colors.background; })->commence());

    auto layout = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->commence();
    layout->setMargin(3);

    auto inner = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.9F, 1.F}})->gap(10)->commence();
    inner->setGrow(true);

    inner->addChild(CTextBuilder::begin()
                        ->text(Text::escapeMarkup(Text::permissionTitle(request.permission)))
                        ->fontSize(CFontSize{CFontSize::HT_FONT_H2})
                        ->color([PALETTE] { return PALETTE()->m_colors.text; })
                        ->commence());

    inner->addChild(CTextBuilder::begin()
                        ->text(Text::promptBody(request))
                        ->fontSize(CFontSize{CFontSize::HT_FONT_TEXT})
                        ->color([PALETTE] { return PALETTE()->m_colors.text; })
                        ->async(false)
                        ->commence());

    const auto BUTTON = [&CHOOSE](std::string label, eChoice choice) {
        return CButtonBuilder::begin()
            ->label(std::move(label))
            ->onMainClick([CHOOSE, choice](auto) { CHOOSE(choice); })
            ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1, 1}})
            ->commence();
    };

    auto buttons = CRowLayoutBuilder::begin()->gap(3)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1, 1}})->commence();
    auto spacer  = CNullBuilder::begin()->commence();
    spacer->setGrow(true);

    buttons->addChild(spacer);
    buttons->addChild(BUTTON("Deny", eChoice::DENY));
    buttons->addChild(BUTTON("Allow for this session", eChoice::ALLOW_SESSION));

    // a session request never offers persistence
    if (request.persist)
        buttons->addChild(BUTTON("Always allow", eChoice::ALLOW_ALWAYS));

    layout->addChild(inner);
    layout->addChild(buttons);
    window->m_rootElement->addChild(layout);

    window->m_events.keyboardKey.listenStatic([CHOOSE](Input::SKeyboardKeyEvent event) {
        if (event.xkbKeysym == XKB_KEY_Escape)
            CHOOSE(eChoice::DENY);
    });

    window->m_events.closeRequest.listenStatic([CHOOSE] { CHOOSE(eChoice::DENY); });

    window->open();

    return window;
}
