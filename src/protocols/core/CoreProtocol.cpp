#include "CoreProtocol.hpp"
#include "BusObject.hpp"
#include "BusObjectHandle.hpp"
#include "BusQuery.hpp"
#include "ConnectionPrincipal.hpp"
#include "Manager.hpp"
#include "PermissionContext.hpp"
#include "../../core/ClientManager.hpp"

#include <common/Log.hpp>

constexpr uint32_t CORE_PROTOCOL_VERSION = 1;

CCoreProtocol::CCoreProtocol(const SP<Hyprwire::IServerSocket>& socket) : m_socket(socket) {
    socket->addImplementation(makeShared<CHpHyprtavernCoreV1Impl>(CORE_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> object) { bindManager(std::move(object)); }));
}

void CCoreProtocol::bindManager(SP<Hyprwire::IObject> object) {
    auto       resource = makeShared<CHpHyprtavernCoreManagerV1Object>(std::move(object));
    const auto CLIENT   = Core::clients->get(resource->getObject()->client());

    if (!CLIENT) {
        resource->error(UINT32_MAX, "unknown client");
        return;
    }

    const auto MANAGER = keep(m_managers, makeShared<CCoreManagerResource>(std::move(resource), CLIENT));

    // barmaids are served while the tavern is still starting up
    if (m_ready || CLIENT->role() != Bus::eProviderRole::NONE)
        MANAGER->sendReady();
}

void CCoreProtocol::setReady() {
    m_ready = true;

    for (const auto& [raw, manager] : m_managers) {
        manager->sendReady();
    }
}

bool CCoreProtocol::ready() const {
    return m_ready;
}

SP<CBusObjectResource> CCoreProtocol::busObject(uint32_t id) const {
    const auto IT = m_busObjects.find(id);
    return IT == m_busObjects.end() ? nullptr : IT->second;
}

bool CCoreProtocol::hasPermissionContext(uint64_t clientID) const {
    return m_contexts.contains(clientID);
}

void CCoreProtocol::destroyResource(CCoreManagerResource* resource) {
    m_managers.erase(resource);
}

void CCoreProtocol::destroyResource(CBusObjectResource* resource) {
    if (const auto IT = m_busObjects.find(resource->id()); IT != m_busObjects.end() && IT->second.get() == resource)
        m_busObjects.erase(IT);
}

void CCoreProtocol::destroyResource(CBusQueryResource* resource) {
    m_queries.erase(resource);
}

void CCoreProtocol::destroyResource(CBusObjectHandleResource* resource) {
    m_handles.erase(resource);
}

void CCoreProtocol::destroyResource(CPermissionContextResource* resource) {
    if (const auto IT = m_contexts.find(resource->clientID()); IT != m_contexts.end() && IT->second.get() == resource)
        m_contexts.erase(IT);
}

void CCoreProtocol::destroyResource(CConnectionPrincipalResource* resource) {
    m_principals.erase(resource);
}
