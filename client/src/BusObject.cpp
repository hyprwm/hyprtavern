#include "BusObject.hpp"
#include "Helpers.hpp"
#include "Tavern.hpp"

#include <common/PropertyName.hpp>
#include <common/Wire.hpp>

#include <algorithm>
#include <optional>

using namespace Hyprtavern;
using namespace Hyprutils::OS;

CBusObject::CBusObject(SP<CTavern> tavern, SP<CCHpHyprtavernBusObjectV1Object> object, SP<Hyprwire::IServerSocket> server) :
    m_tavern(std::move(tavern)), m_object(std::move(object)), m_server(std::move(server)) {
    m_object->setNewFd([this](int fd, const char* token, const std::vector<const char*>& scope) { onNewFd(fd, token, scope); });
    m_object->setFailed([this] { m_failed = true; });

    m_object->setExposeProtocolError([this](uint32_t reason, const char* name) {
        const auto ERROR = reason > sc<uint32_t>(eExposeError::INVALID_REGISTRATION) ? eExposeError::INVALID_REGISTRATION : sc<eExposeError>(reason);
        m_events.exposeFailed.emit(name, ERROR);
    });
}

CBusObject::~CBusObject() {
    // the server may outlive us through objects held elsewhere
    for (const auto& [raw, client] : m_clients) {
        if (const auto CLIENT = client.client.lock())
            CLIENT->setDisconnectHandler({});
    }

    m_tavern->retire(m_object);
}

void CBusObject::expose(const SExposeOptions& options) {
    if (!options.implementation) {
        m_events.exposeFailed.emit("", eExposeError::INVALID_REGISTRATION);
        return;
    }

    const auto NAME            = options.implementation->protocol()->specName();
    const auto IMPLEMENTATIONS = options.implementation->implementation();

    // the implementation's own, unless told otherwise
    auto version = options.version;
    if (!version && !IMPLEMENTATIONS.empty())
        version = IMPLEMENTATIONS.front()->version;

    // the tavern disconnects us over invalid permissions
    if (NAME.empty() || version == 0 || !validString(NAME) || !validPermissions(options.requiredPermissions)) {
        m_events.exposeFailed.emit(NAME, eExposeError::INVALID_REGISTRATION);
        return;
    }

    // a replaced registration keeps the implementation it was first served with
    if (m_exposed.emplace(NAME).second) {
        auto implementation = options.implementation;
        m_server->addImplementation(std::move(implementation));
    }

    m_object->sendExposeProtocol(NAME.c_str(), version, Wire::toCStrings(options.requiredPermissions), options.exclusive);
}

std::expected<void, eExposeError> CBusObject::exposeSync(const SExposeOptions& options) {
    const auto                  NAME = options.implementation ? options.implementation->protocol()->specName() : std::string{};
    std::optional<eExposeError> error;

    const auto                  LISTENER = m_events.exposeFailed.listen([&error, &NAME](const std::string& name, eExposeError reason) {
        if (name == NAME)
            error = reason;
    });

    expose(options);

    // a refusal arrives before the roundtrip completes
    if (!error)
        m_tavern->m_socket->roundtrip();

    if (error)
        return std::unexpected(*error);

    return {};
}

bool CBusObject::setProperty(const std::string& name, const std::string& value) {
    // the tavern disconnects us over invalid properties
    if (!Bus::isValidPropertyName(name) || !Bus::isValidPropertyValue(value) || !validString(value))
        return false;

    m_object->sendExposeProperty(name.c_str(), value.c_str());
    return true;
}

bool CBusObject::requirePermissions(const std::vector<std::string>& permissions) {
    if (!validPermissions(permissions))
        return false;

    m_object->sendRequirePermissions(Wire::toCStrings(permissions));
    return true;
}

int CBusObject::fd() {
    return m_server->extractLoopFD();
}

bool CBusObject::dispatch() {
    return m_server->dispatchEvents(false);
}

const SPrincipal* CBusObject::principal(const SP<Hyprwire::IServerClient>& client) const {
    const auto IT = m_clients.find(client.get());
    return IT == m_clients.end() ? nullptr : &IT->second.principal;
}

void CBusObject::onNewFd(int fd, const char* token, const std::vector<const char*>& scope) {
    // nothing is served before the principal is known, so the client is only added after redemption
    m_tavern->redeem(token, [self = m_self, owned = makeShared<CFileDescriptor>(fd), scope = Wire::toStrings(scope)](std::optional<SPrincipal> principal) {
        const auto SELF = self.lock();
        if (!SELF || !principal)
            return;

        principal->scope = scope;
        SELF->addClient(std::move(*owned), std::move(*principal));
    });
}

void CBusObject::addClient(CFileDescriptor fd, SPrincipal principal) {
    const auto CLIENT = m_server->addClient(fd.take());
    if (!CLIENT)
        return;

    // before the handshake, so nothing outside the scope is ever advertised or bindable
    CLIENT->setProtocolFilter([scope = principal.scope](std::string_view name) { return std::ranges::contains(scope, name); });

    m_clients[CLIENT.get()] = {.client = CLIENT, .principal = std::move(principal)};

    // registered after, as it runs right away for a client that's already gone
    CLIENT->setDisconnectHandler([this, raw = CLIENT.get()] { m_clients.erase(raw); });
}
