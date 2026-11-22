#include "Core.hpp"
#include "../protocols/KvProtocol.hpp"

#include <common/Log.hpp>

#include <algorithm>
#include <format>

constexpr uint32_t         CORE_PROTOCOL_VERSION     = 1;
constexpr uint32_t         KV_STORE_PROTOCOL_VERSION = 1;
constexpr std::string_view BARMAID_PROTOCOL          = "hp_hyprtavern_barmaid_v1";
constexpr std::string_view KV_STORE_PROTOCOL         = "hp_hyprtavern_kv_store_v1";

namespace {
    std::string_view identityKindName(uint32_t kind) {
        switch (kind) {
            case HP_HYPRTAVERN_CORE_V1_APPLICATION_IDENTITY_KIND_HOST_EXECUTABLE: return "host_executable";
            case HP_HYPRTAVERN_CORE_V1_APPLICATION_IDENTITY_KIND_FLATPAK: return "flatpak";
            case HP_HYPRTAVERN_CORE_V1_APPLICATION_IDENTITY_KIND_INTERNAL: return "internal";
            default: return "";
        }
    }
}

CCore::CCore(UP<Kv::CStore>&& store) : m_store(std::move(store)) {
    ;
}

CCore::~CCore() {
    m_pending.clear();
    m_busObject.reset();
    m_manager.reset();
    m_tavern.reset();

    // tears down all clients, which still need the protocols
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

    // the control channel is the tavern itself
    addClient(controlFd, {.tavern = true, .appScope = "internal:hyprtavern"}, [](std::string_view name) { return name == BARMAID_PROTOCOL || name == KV_STORE_PROTOCOL; }, true);

    if (!connectToTavern(tavernFd))
        return false;

    m_loop.addFd(m_server->extractLoopFD(), [this] { m_server->dispatchEvents(false); });

    m_loop.addFd(m_tavern->extractLoopFD(), [this] {
        if (!m_tavern->dispatchEvents(false)) {
            g_logger->log(LOG_ERR, "lost the connection to the tavern");
            m_loop.exit();
            return;
        }

        std::erase_if(m_pending, [](const auto& connection) { return connection->done; });
    });

    m_barmaid->setReady();

    return true;
}

bool CCore::run() {
    return m_loop.run();
}

const SAccess* CCore::access(const SP<Hyprwire::IServerClient>& client) const {
    const auto IT = m_clients.find(client.get());
    return IT == m_clients.end() ? nullptr : &IT->second;
}

bool CCore::connectToTavern(int fd) {
    m_tavern = Hyprwire::IClientSocket::open(fd);

    auto       impl = makeShared<CCHpHyprtavernCoreV1Impl>(CORE_PROTOCOL_VERSION);
    const auto SPEC = impl->protocol();
    m_tavern->addImplementation(std::move(impl));

    if (!m_tavern->waitForHandshake())
        return false;

    auto object = m_tavern->bindProtocol(SPEC, CORE_PROTOCOL_VERSION);
    if (!object)
        return false;

    m_manager = makeShared<CCHpHyprtavernCoreManagerV1Object>(std::move(object));

    bool ready = false;
    m_manager->setReady([&ready] { ready = true; });

    while (!ready) {
        if (!m_tavern->dispatchEvents(true))
            return false;
    }

    m_manager->setReady([] {});

    bool failed = false;
    m_busObject = makeShared<CCHpHyprtavernBusObjectV1Object>(m_manager->sendGetBusObject("hyprtavern-kv", {}));
    m_busObject->setNewFd([this](int fd, const char* token, const std::vector<const char*>& scope) { onNewFd(fd, token, scope); });
    m_busObject->setExposeProtocolError([&failed](uint32_t reason, const char* name) {
        g_logger->log(LOG_ERR, "the tavern refused to expose {}: {}", name, reason);
        failed = true;
    });

    m_busObject->sendExposeProtocol(KV_STORE_PROTOCOL.data(), KV_STORE_PROTOCOL_VERSION, {}, 1);

    // any refusal arrives before the roundtrip completes
    m_tavern->roundtrip();
    m_busObject->setExposeProtocolError([](uint32_t, const char*) {});

    return !failed;
}

void CCore::onNewFd(int fd, const char* token, const std::vector<const char*>& scope) {
    Hyprutils::OS::CFileDescriptor owned{fd};

    // the store is all we serve
    if (!std::ranges::any_of(scope, [](std::string_view protocol) { return protocol == KV_STORE_PROTOCOL; }))
        return;

    auto object = m_manager->sendRedeemConnectionPrincipalToken(token);
    if (!object)
        return;

    // values are only served once the principal is known, so the client is added after redemption
    auto connection = m_pending.emplace_back(makeUnique<SPendingConnection>()).get();
    connection->fd  = std::move(owned);

    connection->principal = makeShared<CCHpHyprtavernConnectionPrincipalV1Object>(std::move(object));

    connection->principal->setAuthority(
        [connection](uint32_t authority, uint32_t) { connection->access.tavern = authority == HP_HYPRTAVERN_CORE_V1_PRINCIPAL_AUTHORITY_INTERNAL; });

    connection->principal->setApplicationIdentity([connection](const char* id, uint32_t kind, uint32_t stable) {
        const auto KIND = identityKindName(kind);
        if (stable && !KIND.empty() && *id)
            connection->access.appScope = std::format("{}:{}", KIND, id);
    });

    connection->principal->setDone([this, connection] {
        connection->done = true;
        addClient(connection->fd.take(), connection->access, [](std::string_view name) { return name == KV_STORE_PROTOCOL; }, false);
    });

    connection->principal->setFailed([connection] {
        g_logger->log(LOG_WARN, "dropping a connection with an invalid principal token");
        connection->done = true;
    });
}

void CCore::addClient(int fd, SAccess access, std::function<bool(std::string_view)>&& filter, bool control) {
    const auto CLIENT = m_server->addClient(fd);
    if (!CLIENT)
        return;

    // before the handshake, so nothing outside the scope is ever advertised or bindable
    CLIENT->setProtocolFilter(std::move(filter));

    m_clients[CLIENT.get()] = std::move(access);

    CLIENT->setDisconnectHandler([this, raw = CLIENT.get(), control] {
        m_clients.erase(raw);

        if (control) {
            g_logger->log(LOG_DEBUG, "the tavern closed the control channel, exiting");
            m_loop.exit();
        }
    });
}
