#include "SecurityIdentity.hpp"

#include "NaiveIdentity.hpp"

using namespace Security;

UP<ISecurityIdentityProvider> Security::identify(SP<Hyprwire::IServerClient> client, ePrincipalAuthority authority) {
    return makeUnique<CNaiveSecurityIdentityProvider>(client, authority);
}
