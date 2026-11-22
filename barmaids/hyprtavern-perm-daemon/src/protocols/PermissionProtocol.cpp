#include "PermissionProtocol.hpp"
#include "../core/Core.hpp"

#include <common/Log.hpp>

#include <algorithm>

constexpr uint32_t PERMISSION_AUTHENTICATION_PROTOCOL_VERSION = 1;

static_assert(sc<uint32_t>(Prompts::eIdentityKind::INTERNAL) == HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_APPLICATION_IDENTITY_KIND_INTERNAL);

CTransactionResource::CTransactionResource(SP<CHpHyprtavernPermissionAuthenticationTransactionV1Object> resource) : m_resource(std::move(resource)) {
    m_resource->setDestroy([this] { PROTO::permissions->destroyResource(this); });
    m_resource->setOnDestroy([this] { PROTO::permissions->destroyResource(this); });

    m_resource->setSetRequestingAppDisplayName([this](const char* name) {
        if (const auto RET = m_state.setDisplayName(name); !RET)
            m_resource->error(UINT32_MAX, RET.error());
    });

    m_resource->setSetRequestingAppAttestedId([this](const char* id, hpHyprtavernPermissionAuthenticationV1ApplicationIdentityKind kind) {
        if (const auto RET = m_state.setAttestedID(id, kind); !RET)
            m_resource->error(UINT32_MAX, RET.error());
    });

    m_resource->setAskPermission([this](const char* permission, hpHyprtavernPermissionAuthenticationV1AskType type) {
        if (const auto RET = m_state.ask(permission, type); !RET) {
            m_resource->error(UINT32_MAX, RET.error());
            return;
        }

        g_core->enqueue(m_self);
    });
}

CTransactionResource::~CTransactionResource() {
    m_resource->setOnDestroy({});

    // dismisses the prompt, if shown
    if (g_core)
        g_core->dismiss(this);
}

const Prompts::SRequest& CTransactionResource::request() const {
    return m_state.request();
}

void CTransactionResource::sendResult(hpHyprtavernPermissionAuthenticationV1ResponseType response) {
    if (m_done)
        return;

    m_done = true;
    m_resource->sendPermissionResult(m_state.request().permission.c_str(), response);
}

CPermissionProtocol::CPermissionProtocol(const SP<Hyprwire::IServerSocket>& socket) : m_socket(socket) {
    socket->addImplementation(makeShared<CHpHyprtavernPermissionAuthenticationV1Impl>(PERMISSION_AUTHENTICATION_PROTOCOL_VERSION,
                                                                                      [this](SP<Hyprwire::IObject> object) { bindManager(std::move(object)); }));
}

CPermissionProtocol::~CPermissionProtocol() {
    for (const auto& manager : m_managers) {
        manager->setOnDestroy({});
    }
}

void CPermissionProtocol::bindManager(SP<Hyprwire::IObject> object) {
    const auto MANAGER = m_managers.emplace_back(makeShared<CHpHyprtavernPermissionAuthenticationManagerV1Object>(std::move(object)));

    MANAGER->setOnDestroy([this, raw = MANAGER.get()] { std::erase_if(m_managers, [raw](const auto& other) { return other.get() == raw; }); });

    MANAGER->setRequestAvailability([this, raw = MANAGER.get()] { raw->sendAvailability(m_available); });

    MANAGER->setInitPermissionTransaction([this, raw = MANAGER.get()](uint32_t seq) {
        const auto SOCKET = m_socket.lock();
        auto object = SOCKET ? SOCKET->createObject(raw->getObject()->client(), raw->getObject(), CHpHyprtavernPermissionAuthenticationTransactionV1Object::name(), seq) : nullptr;
        if (!object)
            return;

        const auto TRANSACTION =
            m_transactions.emplace_back(makeShared<CTransactionResource>(makeShared<CHpHyprtavernPermissionAuthenticationTransactionV1Object>(std::move(object))));
        TRANSACTION->m_self = TRANSACTION;
    });

    MANAGER->sendAvailability(m_available);
}

void CPermissionProtocol::destroyResource(CTransactionResource* resource) {
    std::erase_if(m_transactions, [resource](const auto& other) { return other.get() == resource; });
}

void CPermissionProtocol::setAvailable(bool available) {
    if (m_available == available)
        return;

    g_logger->log(LOG_DEBUG, "permission prompts are now {}", available ? "available" : "unavailable");

    m_available = available;

    for (const auto& manager : m_managers) {
        manager->sendAvailability(m_available);
    }
}

bool CPermissionProtocol::available() const {
    return m_available;
}
