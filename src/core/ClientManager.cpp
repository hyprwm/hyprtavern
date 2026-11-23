#include "ClientManager.hpp"

#include <common/Log.hpp>

using namespace Core;

CClientManager::CClientManager(bool enforcePermissions) : m_enforcePermissions(enforcePermissions) {
    ;
}

SP<CClient> CClientManager::add(const SP<Hyprwire::IServerClient>& wire, Bus::eProviderRole role, std::optional<Security::SAppIdentity> internalIdentity) {
    if (auto existing = get(wire))
        return existing;

    const auto           CREDENTIALS = wire->getPeerCredentials();

    Security::SPrincipal principal = {.pid = CREDENTIALS ? CREDENTIALS->pid : -1};

    if (internalIdentity) {
        principal.authority = Security::eAuthority::INTERNAL;
        principal.identity  = std::move(*internalIdentity);
    } else if (CREDENTIALS)
        principal.identity = Security::probeProcess(CREDENTIALS->pid, CREDENTIALS->uid);

    auto client = makeShared<CClient>(wire, m_nextID++, principal, Security::policyFor(principal, m_enforcePermissions), role);

    m_clients[wire.get()] = client;
    m_ids[client->id()]   = client;

    g_logger->log(LOG_DEBUG, "client {} connected: pid {}, identity {} ({})", client->id(), principal.pid, principal.identity.id, sc<int>(principal.identity.kind));

    // runs after the client's wire objects are torn down
    wire->setDisconnectHandler([this, raw = wire.get(), id = client->id()] {
        g_logger->log(LOG_DEBUG, "client {} disconnected", id);
        m_clients.erase(raw);
        m_ids.erase(id);
    });

    return client;
}

SP<CClient> CClientManager::get(const SP<Hyprwire::IServerClient>& wire) const {
    const auto IT = m_clients.find(wire.get());

    // a stale entry never matches, its connection is gone
    return IT != m_clients.end() && IT->second->wire() == wire ? IT->second : nullptr;
}

SP<CClient> CClientManager::get(uint64_t id) const {
    const auto IT = m_ids.find(id);
    return IT == m_ids.end() ? nullptr : IT->second.lock();
}
