#pragma once

#include "../core/Transaction.hpp"

#include <string>
#include <string_view>

// prompt texts, as Pango markup
namespace Text {
    // for untrusted strings: escapes markup and replaces control and bidi override characters
    std::string escapeMarkup(std::string_view text);

    // a short title for the permission
    std::string permissionTitle(std::string_view permission);

    // who is asking, for what, and what allowing it means
    std::string promptBody(const Prompts::SRequest& request);
}
