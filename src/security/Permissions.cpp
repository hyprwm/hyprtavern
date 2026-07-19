#include "Permissions.hpp"

#include <algorithm>

using namespace Security;

static bool permissionIsInRange(uint32_t held, uint32_t requested) {
    if (held == requested)
        return true;

    if (held == 0)
        return false;

    // The current core permission enum uses N0000 for top-level groups and NN000 for subgroups.
    if (held % 10000 == 0)
        return requested >= held && requested < held + 10000;

    if (held % 1000 == 0)
        return requested >= held && requested < held + 1000;

    return false;
}

bool Security::permissionImplies(uint32_t held, uint32_t requested) {
    if (requested == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_UNOBTAINIUM)
        return held == requested;

    if (held == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_TYPE_TAVERNKEEP)
        return true;

    return permissionIsInRange(held, requested);
}

bool Security::permissionListHas(const std::vector<uint32_t>& held, uint32_t requested) {
    return std::ranges::any_of(held, [&requested](const auto& e) { return permissionImplies(e, requested); });
}
