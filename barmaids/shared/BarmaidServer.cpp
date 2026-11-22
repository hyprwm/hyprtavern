#include "BarmaidServer.hpp"

#include <common/Environment.hpp>
#include <common/Log.hpp>

#include <algorithm>

constexpr uint32_t BARMAID_PROTOCOL_VERSION = 1;

CBarmaidServer::CBarmaidServer(const SP<Hyprwire::IServerSocket>& socket) {
    socket->addImplementation(makeShared<CHpHyprtavernBarmaidV1Impl>(BARMAID_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> object) { bindManager(std::move(object)); }));
}

CBarmaidServer::~CBarmaidServer() {
    for (const auto& manager : m_managers) {
        manager->setOnDestroy({});
    }
}

void CBarmaidServer::bindManager(SP<Hyprwire::IObject> object) {
    const auto MANAGER = m_managers.emplace_back(makeShared<CHpHyprtavernBarmaidManagerV1Object>(std::move(object)));

    MANAGER->setOnDestroy([this, raw = MANAGER.get()] { std::erase_if(m_managers, [raw](const auto& other) { return other.get() == raw; }); });

    // no response while starting, ready is sent once initialization completes
    MANAGER->setRequestStatus([this, raw = MANAGER.get()] {
        if (m_ready)
            raw->sendReady();
    });

    // allowed before ready, so the tavern can provide the startup display configuration
    MANAGER->setUpdateTavernEnvironment([this, raw = MANAGER.get()](const std::vector<const char*>& names, const std::vector<const char*>& values) {
        if (const auto RET = Environment::validateUpdate(names, values); !RET) {
            raw->error(UINT32_MAX, RET.error());
            return;
        }

        g_logger->log(LOG_DEBUG, "updating {} environment variables", names.size());

        Environment::applyUpdate(names, values);
        m_events.environmentChanged.emit();
    });

    if (m_ready)
        MANAGER->sendReady();
}

void CBarmaidServer::setReady() {
    if (m_ready)
        return;

    m_ready = true;

    for (const auto& manager : m_managers) {
        manager->sendReady();
    }
}
