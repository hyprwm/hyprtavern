#include "SecurityIdentity.hpp"

#include "NaiveIdentity.hpp"

using namespace Security;

UP<ISecurityIdentityProvider> Security::identify(SP<Hyprwire::IServerClient> client) {
    // FIXME: flatpak!!
    return makeUnique<CNaiveSecurityIdentityProvider>(client);
}
