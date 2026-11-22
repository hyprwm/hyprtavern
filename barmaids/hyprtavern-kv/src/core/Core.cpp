#include "Core.hpp"
#include "../protocols/KvProtocol.hpp"

#include <common/Log.hpp>

#include <format>

constexpr std::string_view BARMAID_PROTOCOL  = "hp_hyprtavern_barmaid_v1";
constexpr std::string_view KV_STORE_PROTOCOL = "hp_hyprtavern_kv_store_v1";

namespace {
    std::string_view identityKindName(Hyprtavern::eIdentityKind kind) {
        switch (kind) {
            case Hyprtavern::eIdentityKind::HOST_EXECUTABLE: return "host_executable";
            case Hyprtavern::eIdentityKind::FLATPAK: return "flatpak";
            case Hyprtavern::eIdentityKind::INTERNAL: return "internal";
            default: return "";
        }
    }
}

CCore::CCore(UP<Kv::CStore>&& store) : m_store(std::move(store)) {
    ;
}

CCore::~CCore() {
    // tears down all routed clients, which still need the protocols
    m_busObject.reset();
    m_tavern.reset();

    // and the control channel
    m_server.reset();

    PROTO::kv.reset();
    m_barmaid.reset();
}

bool CCore::init(int controlFd, int tavernFd) {
    m_server = Hyprwire::IServerSocket::open();
    if (!m_server)
        return false;

    m_barmaid = makeUnique<CBarmaidServer>(m_server);
    PROTO::kv = makeUnique<CKvProtocol>(m_server);

    m_control = m_server->addClient(controlFd);
    if (!m_control)
        return false;

    // before the handshake, so nothing else is ever advertised or bindable
    m_control->setProtocolFilter([](std::string_view name) { return name == BARMAID_PROTOCOL || name == KV_STORE_PROTOCOL; });

    m_control->setDisconnectHandler([this] {
        g_logger->log(LOG_DEBUG, "the tavern closed the control channel, exiting");
        m_loop.exit();
    });

    if (!connectToTavern(tavernFd))
        return false;

    m_loop.addFd(m_server->extractLoopFD(), [this] { m_server->dispatchEvents(false); });

    m_loop.addFd(m_tavern->fd(), [this] {
        if (!m_tavern->dispatch()) {
            g_logger->log(LOG_ERR, "lost the connection to the tavern");
            m_loop.exit();
        }
    });

    m_loop.addFd(m_busObject->fd(), [this] { m_busObject->dispatch(); });

    m_barmaid->setReady();

    return true;
}

bool CCore::run() {
    return m_loop.run();
}

std::optional<SAccess> CCore::access(const SP<Hyprwire::IServerClient>& client) const {
    // the control channel is the tavern itself
    if (client == m_control)
        return SAccess{.tavern = true, .appScope = "internal:hyprtavern"};

    const auto PRINCIPAL = m_busObject ? m_busObject->principal(client) : nullptr;
    if (!PRINCIPAL)
        return std::nullopt;

    SAccess    access = {.tavern = PRINCIPAL->internal};

    const auto KIND = identityKindName(PRINCIPAL->identity.kind);
    if (PRINCIPAL->identity.stable && !KIND.empty() && !PRINCIPAL->identity.id.empty())
        access.appScope = std::format("{}:{}", KIND, PRINCIPAL->identity.id);

    return access;
}

bool CCore::connectToTavern(int fd) {
    auto tavern = Hyprtavern::ITavern::open({.persistPermissions = false, .fd = fd});
    if (!tavern) {
        g_logger->log(LOG_ERR, "failed to connect to the tavern: {}", Hyprtavern::errorString(tavern.error()));
        return false;
    }

    m_tavern = *tavern;

    auto object = m_tavern->createObject("hyprtavern-kv");
    if (!object)
        return false;

    m_busObject = *object;

    // the store must be exposed exclusively
    if (const auto RET = m_busObject->exposeSync({.implementation = PROTO::kv->implementation(), .exclusive = true}); !RET) {
        g_logger->log(LOG_ERR, "the tavern refused to expose the store: {}", sc<int>(RET.error()));
        return false;
    }

    return true;
}
