#include "BusObjectHandle.hpp"
#include "Arguments.hpp"
#include "BusObject.hpp"
#include "CoreProtocol.hpp"
#include "../../bus/PrincipalTokens.hpp"
#include "../../bus/Registry.hpp"

#include <common/Wire.hpp>

#include <hyprutils/os/FileDescriptor.hpp>

#include <algorithm>
#include <format>
#include <unordered_set>

#include <sys/socket.h>

using namespace Hyprutils::OS;

CBusObjectHandleResource::CBusObjectHandleResource(SP<CHpHyprtavernBusObjectHandleV1Object> resource, SP<Core::CClient> client, uint32_t id) :
    m_resource(std::move(resource)), m_client(client), m_id(id) {
    m_resource->setDestroy([this] { PROTO::core->destroyResource(this); });
    m_resource->setOnDestroy([this] { PROTO::core->destroyResource(this); });

    m_resource->setConnect([this](const std::vector<const char*>& protocols) { onConnect(protocols); });

    // missing and invisible objects are indistinguishable
    const auto OBJECT = Bus::registry->get(m_id);
    if (!OBJECT || !client->canInspect(*OBJECT)) {
        m_resource->sendFailed();
        return;
    }

    std::vector<const char*> protocols;
    std::vector<uint32_t>    versions;

    for (const auto& [name, protocol] : OBJECT->protocols) {
        protocols.emplace_back(name.c_str());
        versions.emplace_back(protocol.version);
    }

    std::vector<std::string> properties;

    for (const auto& [name, value] : OBJECT->properties) {
        properties.emplace_back(std::format("{}={}", name, value));
    }

    m_resource->sendName(OBJECT->name.c_str());
    m_resource->sendProtocols(protocols, versions);
    m_resource->sendProperties(Wire::toCStrings(properties));
    m_resource->sendProviderIdentity(OBJECT->provider.id.c_str(), sc<uint32_t>(OBJECT->provider.kind), OBJECT->provider.stable);
    m_resource->sendDone();

    m_state = eState::READY;
}

CBusObjectHandleResource::~CBusObjectHandleResource() {
    m_resource->setOnDestroy({});
}

void CBusObjectHandleResource::onConnect(const std::vector<const char*>& protocols) {
    if (m_state != eState::READY) {
        m_resource->error(Arguments::MALFORMED, "connect is only allowed once, after done");
        return;
    }

    const auto                           REQUESTED = Wire::toStrings(protocols);
    std::unordered_set<std::string_view> unique{REQUESTED.begin(), REQUESTED.end()};

    if (REQUESTED.empty() || !Arguments::validArray(protocols) || unique.size() != REQUESTED.size()) {
        m_resource->error(Arguments::MALFORMED, "connect requires a nonempty list of distinct protocols");
        return;
    }

    m_state = eState::CONNECTED;

    if (const auto ERROR = admit(REQUESTED))
        m_resource->sendSocketFailed(*ERROR);
}

std::optional<hpHyprtavernCoreV1BusObjectConnectionError> CBusObjectHandleResource::admit(const std::vector<std::string>& protocols) {
    const auto OBJECT   = Bus::registry->get(m_id);
    const auto PROVIDER = PROTO::core->busObject(m_id);
    const auto CLIENT   = m_client.lock();

    if (!OBJECT || !PROVIDER || !CLIENT)
        return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR;

    if (!std::ranges::all_of(protocols, [&OBJECT](const auto& name) { return OBJECT->protocol(name); }))
        return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_BAD_PROTOCOL;

    // requirements are checked again on every admission, metadata is not a reservation
    if (!CLIENT->hasPermissions(OBJECT->requirements))
        return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_INSUFFICIENT_PERMISSIONS;

    for (const auto& name : protocols) {
        if (!CLIENT->hasPermissions(OBJECT->protocol(name)->requirements))
            return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_INSUFFICIENT_PERMISSIONS;
    }

    const auto TOKEN = Bus::tokens->issue(CLIENT->snapshot(), CLIENT->id(), OBJECT->owner);
    if (TOKEN.empty())
        return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR;

    int fds[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0)
        return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_UNKNOWN_ERROR;

    // the messages carry copies of both ends, ours close here
    const CFileDescriptor REQUESTER_END{fds[0]}, PROVIDER_END{fds[1]};

    m_resource->sendSocket(REQUESTER_END.get());
    PROVIDER->sendNewFd(PROVIDER_END.get(), TOKEN, protocols);

    return std::nullopt;
}
