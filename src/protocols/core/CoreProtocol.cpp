#include "CoreProtocol.hpp"
#include "BusObject.hpp"
#include "BusObjectHandle.hpp"
#include "BusQuery.hpp"
#include "ConnectionPrincipal.hpp"
#include "Manager.hpp"
#include "PermissionContext.hpp"
#include "../../core/ClientManager.hpp"

#include <common/Log.hpp>

#include <algorithm>

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

    const auto MANAGER = m_managers.emplace_back(makeShared<CCoreManagerResource>(std::move(resource), CLIENT));

    // barmaids are served while the tavern is still starting up
    if (m_ready || CLIENT->role() != Bus::eProviderRole::NONE)
        MANAGER->sendReady();
}

void CCoreProtocol::setReady() {
    m_ready = true;

    for (const auto& manager : m_managers) {
        manager->sendReady();
    }
}

bool CCoreProtocol::ready() const {
    return m_ready;
}

SP<CBusObjectResource> CCoreProtocol::busObject(uint32_t id) const {
    const auto IT = std::ranges::find_if(m_busObjects, [id](const auto& object) { return object->id() == id; });
    return IT == m_busObjects.end() ? nullptr : *IT;
}

bool CCoreProtocol::hasPermissionContext(uint64_t clientID) const {
    return std::ranges::any_of(m_contexts, [clientID](const auto& context) { return context->clientID() == clientID; });
}

void CCoreProtocol::destroyResource(CCoreManagerResource* resource) {
    std::erase_if(m_managers, [resource](const auto& other) { return other.get() == resource; });
}

void CCoreProtocol::destroyResource(CBusObjectResource* resource) {
    std::erase_if(m_busObjects, [resource](const auto& other) { return other.get() == resource; });
}

void CCoreProtocol::destroyResource(CBusQueryResource* resource) {
    std::erase_if(m_queries, [resource](const auto& other) { return other.get() == resource; });
}

void CCoreProtocol::destroyResource(CBusObjectHandleResource* resource) {
    std::erase_if(m_handles, [resource](const auto& other) { return other.get() == resource; });
}

void CCoreProtocol::destroyResource(CPermissionContextResource* resource) {
    std::erase_if(m_contexts, [resource](const auto& other) { return other.get() == resource; });
}

void CCoreProtocol::destroyResource(CConnectionPrincipalResource* resource) {
    std::erase_if(m_principals, [resource](const auto& other) { return other.get() == resource; });
}
