#pragma once

#include "../core/Transaction.hpp"

#include <common/Memory.hpp>

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/window/Window.hpp>

#include <functional>

namespace UI {
    enum class eChoice : uint8_t {
        DENY,
        ALLOW_SESSION,
        ALLOW_ALWAYS,
    };

    // opens a prompt for a frozen request. onChoice runs once, closing the window counts as denying.
    SP<Hyprtoolkit::IWindow> openPrompt(const SP<Hyprtoolkit::IBackend>& backend, const Prompts::SRequest& request, std::function<void(eChoice)>&& onChoice);
}
