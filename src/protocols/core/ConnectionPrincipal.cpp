#include "ConnectionPrincipal.hpp"
#include "Arguments.hpp"
#include "CoreProtocol.hpp"
#include "../../core/ClientManager.hpp"

#include <common/Wire.hpp>

CConnectionPrincipalResource::CConnectionPrincipalResource(SP<CHpHyprtavernConnectionPrincipalV1Object> resource, std::optional<Bus::SRedeemedPrincipal> principal) :
    m_resource(std::move(resource)), m_principal(std::move(principal)) {
    m_resource->setDestroy([this] { PROTO::core->destroyResource(this); });
    m_resource->setOnDestroy([this] { PROTO::core->destroyResource(this); });

    m_resource->setRequery([this] { onRequery(); });

    if (!m_principal) {
        fail();
        return;
    }

    // the admission snapshot, even if the client is gone by now
    sendSnapshot(m_principal->snapshot);
}

CConnectionPrincipalResource::~CConnectionPrincipalResource() {
    m_resource->setOnDestroy({});
}

void CConnectionPrincipalResource::onRequery() {
    if (!m_principal) {
        m_resource->error(HP_HYPRTAVERN_CORE_V1_PROTOCOL_ERRORS_INVALID_STATE, "requery on an inert principal");
        return;
    }

    // client ids are never reused, so this can't be a replacement connection
    const auto CLIENT = Core::clients->get(m_principal->source);
    if (!CLIENT) {
        fail();
        return;
    }

    // live policy, display and grants, but the admitted identity stays fixed
    auto snapshot      = CLIENT->snapshot();
    snapshot.principal = m_principal->snapshot.principal;

    sendSnapshot(snapshot);
}

void CConnectionPrincipalResource::sendSnapshot(const Bus::SPrincipalSnapshot& snapshot) {
    const auto& PRINCIPAL = snapshot.principal;

    m_resource->sendAuthority(sc<uint32_t>(PRINCIPAL.authority), sc<uint32_t>(snapshot.policy));
    m_resource->sendProcessIdentity(PRINCIPAL.pid < 0 ? 0 : PRINCIPAL.pid);
    m_resource->sendApplicationIdentity(PRINCIPAL.identity.id.c_str(), sc<uint32_t>(PRINCIPAL.identity.kind), PRINCIPAL.identity.stable);
    m_resource->sendDisplayIdentity(snapshot.display.name.c_str(), snapshot.display.description.c_str());
    m_resource->sendExplicitPermissionGrants(Wire::toCStrings(snapshot.grants));
    m_resource->sendDone();
}

void CConnectionPrincipalResource::fail() {
    m_principal.reset();
    m_resource->sendFailed();
}
