#include "ClientManager.hpp"

#include <common/Log.hpp>

#include <algorithm>

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

    auto client = m_clients.emplace_back(makeShared<CClient>(wire, m_nextID++, principal, Security::policyFor(principal, m_enforcePermissions), role));

    g_logger->log(LOG_DEBUG, "client {} connected: pid {}, identity {} ({})", client->id(), principal.pid, principal.identity.id, sc<int>(principal.identity.kind));

    // runs after the client's wire objects are torn down
    wire->setDisconnectHandler([this, weak = WP<CClient>{client}] {
        if (const auto CLIENT = weak.lock()) {
            g_logger->log(LOG_DEBUG, "client {} disconnected", CLIENT->id());
            std::erase(m_clients, CLIENT);
        }
    });

    return client;
}

SP<CClient> CClientManager::get(const SP<Hyprwire::IServerClient>& wire) const {
    const auto IT = std::ranges::find_if(m_clients, [&wire](const auto& client) { return client->wire() == wire; });
    return IT == m_clients.end() ? nullptr : *IT;
}

SP<CClient> CClientManager::get(uint64_t id) const {
    const auto IT = std::ranges::find_if(m_clients, [id](const auto& client) { return client->id() == id; });
    return IT == m_clients.end() ? nullptr : *IT;
}
