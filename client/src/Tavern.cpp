#include "Tavern.hpp"

#include <common/AppID.hpp>
#include <common/Environment.hpp>
#include <common/Limits.hpp>
#include <common/PropertyName.hpp>
#include <common/Wire.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <optional>
#include <unordered_set>

#include <unistd.h>

using namespace Hyprtavern;
using namespace Hyprutils::OS;

namespace {
    constexpr uint32_t          CORE_PROTOCOL_VERSION = 1;

    SP<Hyprwire::IClientSocket> openSocket(const STavernOptions& options) {
        if (options.fd)
            return Hyprwire::IClientSocket::open(*options.fd);

        const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
        if (!runtimeDir || !*runtimeDir)
            return nullptr;

        return Hyprwire::IClientSocket::open(std::format("{}/hyprtavern/ht.sock", runtimeDir));
    }

    std::expected<SP<Hyprwire::IClientSocket>, eError> openConnection(CFileDescriptor fd, const ClientImplementations& implementations) {
        auto socket = Hyprwire::IClientSocket::open(fd.take());
        if (!socket)
            return std::unexpected(eError::DISCONNECTED);

        for (auto implementation : implementations) {
            socket->addImplementation(std::move(implementation));
        }

        if (!socket->waitForHandshake())
            return std::unexpected(eError::HANDSHAKE_FAILED);

        return socket;
    }
}

std::expected<SP<ITavern>, eError> ITavern::open(const STavernOptions& options, std::chrono::milliseconds timeout) {
    const auto DEADLINE = Clock::now() + timeout;

    // an invalid display identity or app id is a protocol error
    if (options.appName.size() > MAX_DISPLAY_NAME_LENGTH || options.appDescription.size() > MAX_DISPLAY_DESCRIPTION_LENGTH || !validString(options.appName) ||
        !validString(options.appDescription) || (!options.appID.empty() && !Security::isValidAppID(options.appID)))
        return std::unexpected(eError::INVALID_ARGUMENT);

    auto socket = openSocket(options);
    if (!socket)
        return std::unexpected(eError::NO_TAVERN);

    auto tavern    = makeShared<CTavern>(std::move(socket));
    tavern->m_self = tavern;

    if (const auto RET = tavern->init(options, DEADLINE); !RET)
        return std::unexpected(RET.error());

    return tavern;
}

CTavern::CTavern(SP<Hyprwire::IClientSocket> socket) : m_socket(std::move(socket)) {
    ;
}

std::expected<void, eError> CTavern::init(const STavernOptions& options, Clock::time_point deadline) {
    auto       implementation = makeShared<CCHpHyprtavernCoreV1Impl>(CORE_PROTOCOL_VERSION);
    const auto SPEC           = implementation->protocol();
    m_socket->addImplementation(std::move(implementation));

    if (!m_socket->waitForHandshake())
        return std::unexpected(eError::HANDSHAKE_FAILED);

    if (!m_socket->getSpec(SPEC->specName()))
        return std::unexpected(eError::UNSUPPORTED);

    // async, so the ready event sent right after binding finds its listener
    m_socket->bindProtocolAsync(SPEC, CORE_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> object) {
        if (object)
            bindManager(std::move(object));
    });

    if (const auto RET = waitUntil([this] { return m_ready; }, deadline); !RET)
        return RET;

    // before anything else, as everything after may depend on our identity
    if (!options.appID.empty())
        m_manager->sendDeclareHostApplication(options.appID.c_str());

    m_permissions = makeUnique<CPermissions>(m_manager, options);

    return {};
}

void CTavern::bindManager(SP<Hyprwire::IObject> object) {
    m_manager = makeShared<CCHpHyprtavernCoreManagerV1Object>(std::move(object));

    m_manager->setReady([this] { m_ready = true; });

    // results come in request order
    m_manager->setUpdateTavernEnvironmentResult([this](uint32_t result) {
        if (m_environmentResults.empty())
            return;

        auto done = std::move(m_environmentResults.front());
        m_environmentResults.pop_front();

        if (result == HP_HYPRTAVERN_CORE_V1_ENVIRONMENT_UPDATE_RESULT_APPLIED)
            done({});
        else
            done(std::unexpected(eError::DENIED));
    });
}

int CTavern::fd() {
    return m_socket->extractLoopFD();
}

bool CTavern::dispatch() {
    const bool OK = m_socket->dispatchEvents(false);
    sweep();
    return OK;
}

void CTavern::sweep() {
    // requests are only done once their callbacks returned, so none of these is running
    std::erase_if(m_finds, [](const auto& request) { return request->done; });
    std::erase_if(m_handles, [](const auto& handle) { return handle->done(); });
    std::erase_if(m_redemptions, [](const auto& redemption) { return redemption->done(); });

    // not yet while starting up
    if (m_permissions)
        m_permissions->sweep();
}

std::expected<void, eError> CTavern::waitUntil(const std::function<bool()>& done, Clock::time_point deadline) {
    return dispatchUntil(fd(), [this] { return dispatch(); }, done, deadline);
}

template <typename T>
std::expected<T, eError> CTavern::await(Clock::time_point deadline, const std::function<void(std::function<void(std::expected<T, eError>)>&&)>& start) {
    // shared, as a request that timed out may still call back later
    auto result = makeShared<std::optional<std::expected<T, eError>>>();

    start([result](std::expected<T, eError> value) { *result = std::move(value); });

    if (const auto RET = waitUntil([result] { return result->has_value(); }, deadline); !RET)
        return std::unexpected(RET.error());

    return std::move(**result);
}

void CTavern::requestPermissions(const std::vector<std::string>& permissions, ePermissionMode mode, PermissionCallback&& done) {
    m_permissions->request(permissions, mode, std::move(done));
}

std::expected<PermissionResults, eError> CTavern::requestPermissionsSync(const std::vector<std::string>& permissions, ePermissionMode mode, std::chrono::milliseconds timeout) {
    return await<PermissionResults>(Clock::now() + timeout, [&](std::function<void(std::expected<PermissionResults, eError>)>&& done) {
        requestPermissions(permissions, mode, [done = std::move(done)](PermissionResults results) { done(std::move(results)); });
    });
}

void CTavern::SFind::finish() {
    callback(std::move(results));
    done = true;
}

void CTavern::find(const SQuery& query, FindCallback&& done) {
    std::vector<std::string> properties;

    for (const auto& [name, value] : query.properties) {
        // the tavern disconnects us over invalid properties
        if (!Bus::isValidPropertyName(name) || !Bus::isValidPropertyValue(value) || !validString(value)) {
            done(std::unexpected(eError::INVALID_ARGUMENT));
            return;
        }

        properties.emplace_back(std::format("{}={}", name, value));
    }

    if (!validStrings(query.protocols) || properties.size() > MAX_ARRAY_ELEMENTS) {
        done(std::unexpected(eError::INVALID_ARGUMENT));
        return;
    }

    auto request      = m_finds.emplace_back(makeUnique<SFind>()).get();
    request->callback = std::move(done);
    request->query =
        makeShared<CCHpHyprtavernBusQueryV1Object>(m_manager->sendGetQueryObject(Wire::toCStrings(query.protocols), sc<hpHyprtavernCoreV1BusQueryFilterMode>(query.protocolMode),
                                                                                 Wire::toCStrings(properties), sc<hpHyprtavernCoreV1BusQueryFilterMode>(query.propertyMode)));

    request->query->setResults([this, request](const std::vector<uint32_t>& ids) { onResults(request, ids); });
}

void CTavern::onResults(SFind* request, const std::vector<uint32_t>& ids) {
    request->query->sendDestroy();
    request->pending = ids.size();

    if (ids.empty()) {
        request->finish();
        return;
    }

    for (const auto ID : ids) {
        m_handles.emplace_back(makeUnique<CObjectHandle>(m_manager, ID, [request](CObjectHandle& handle, bool found) {
            // gone or hidden since the query, so not a match anymore
            if (found)
                request->results.emplace_back(handle.m_info);

            if (--request->pending == 0)
                request->finish();
        }));
    }
}

std::expected<std::vector<SObjectInfo>, eError> CTavern::findSync(const SQuery& query, std::chrono::milliseconds timeout) {
    return findUntil(query, Clock::now() + timeout);
}

std::expected<std::vector<SObjectInfo>, eError> CTavern::findUntil(const SQuery& query, Clock::time_point deadline) {
    return await<std::vector<SObjectInfo>>(deadline, [this, &query](FindCallback&& done) { find(query, std::move(done)); });
}

void CTavern::connect(uint32_t objectID, const ClientImplementations& implementations, ConnectCallback&& done) {
    std::vector<std::string> protocols;

    for (const auto& implementation : implementations) {
        if (!implementation) {
            done(std::unexpected(eError::INVALID_ARGUMENT));
            return;
        }

        protocols.emplace_back(implementation->protocol()->specName());
    }

    // an empty or duplicate protocol list is a protocol error
    const std::unordered_set<std::string> UNIQUE{protocols.begin(), protocols.end()};
    if (protocols.empty() || UNIQUE.size() != protocols.size() || !validStrings(protocols)) {
        done(std::unexpected(eError::INVALID_ARGUMENT));
        return;
    }

    m_handles.emplace_back(makeUnique<CObjectHandle>(m_manager, objectID, [protocols, implementations, done = std::move(done)](CObjectHandle& handle, bool found) mutable {
        if (!found) {
            done(std::unexpected(eError::NOT_FOUND));
            return;
        }

        handle.connect(protocols, [implementations, done = std::move(done)](std::expected<CFileDescriptor, eError> fd) {
            if (!fd) {
                done(std::unexpected(fd.error()));
                return;
            }

            done(openConnection(std::move(*fd), implementations));
        });
    }));
}

std::expected<SP<Hyprwire::IClientSocket>, eError> CTavern::connectSync(uint32_t objectID, const ClientImplementations& implementations, std::chrono::milliseconds timeout) {
    return connectUntil(objectID, implementations, Clock::now() + timeout);
}

std::expected<SP<Hyprwire::IClientSocket>, eError> CTavern::connectSync(const SQuery& query, const ClientImplementations& implementations, std::chrono::milliseconds timeout) {
    return connectUntil(query, implementations, Clock::now() + timeout);
}

std::expected<SP<Hyprwire::IClientSocket>, eError> CTavern::connectUntil(uint32_t objectID, const ClientImplementations& implementations, Clock::time_point deadline) {
    return await<SP<Hyprwire::IClientSocket>>(deadline, [&](ConnectCallback&& done) { connect(objectID, implementations, std::move(done)); });
}

std::expected<SP<Hyprwire::IClientSocket>, eError> CTavern::connectUntil(const SQuery& query, const ClientImplementations& implementations, Clock::time_point deadline) {
    const auto OBJECTS = findUntil(query, deadline);
    if (!OBJECTS)
        return std::unexpected(OBJECTS.error());

    auto error = eError::NOT_FOUND;

    for (const auto& object : *OBJECTS) {
        const bool EXPOSES_ALL = std::ranges::all_of(
            implementations, [&object](const auto& implementation) { return !implementation || object.protocols.contains(implementation->protocol()->specName()); });

        if (!EXPOSES_ALL)
            continue;

        auto socket = connectUntil(object.id, implementations, deadline);
        if (socket)
            return socket;

        // later objects can't fix the request, the deadline or the connection
        error = socket.error();
        if (error == eError::INVALID_ARGUMENT || error == eError::TIMED_OUT || error == eError::DISCONNECTED)
            break;
    }

    return std::unexpected(error);
}

std::expected<SP<IBusObject>, eError> CTavern::createObject(const std::string& name, const std::vector<std::string>& requiredPermissions) {
    // the tavern disconnects us over invalid names or permissions
    if (name.size() > MAX_OBJECT_NAME_LENGTH || !validString(name) || !validPermissions(requiredPermissions))
        return std::unexpected(eError::INVALID_ARGUMENT);

    auto server = Hyprwire::IServerSocket::open();
    if (!server)
        return std::unexpected(eError::UNKNOWN);

    auto wire = makeShared<CCHpHyprtavernBusObjectV1Object>(m_manager->sendGetBusObject(name.c_str(), Wire::toCStrings(requiredPermissions)));

    auto object    = makeShared<CBusObject>(m_self.lock(), std::move(wire), std::move(server));
    object->m_self = object;

    // a refusal arrives before the roundtrip completes
    m_socket->roundtrip();

    if (object->m_failed)
        return std::unexpected(eError::BUS_FULL);

    return object;
}

void CTavern::redeem(const std::string& token, CPrincipalRedemption::DoneFn&& done) {
    m_redemptions.emplace_back(makeUnique<CPrincipalRedemption>(m_manager, token, std::move(done)));
}

void CTavern::retire(SP<CCHpHyprtavernBusObjectV1Object> object) {
    object->setNewFd([](int fd, const char*, const std::vector<const char*>&) { close(fd); });
    object->setExposeProtocolError([](uint32_t, const char*) {});
    object->sendDestroy();

    // dropping it would make those events reference an unknown object, which breaks the connection
    m_retiredObjects.emplace_back(std::move(object));
}

void CTavern::updateEnvironment(const EnvironmentUpdate& environment, EnvironmentCallback&& done) {
    std::vector<const char*> names, values;

    for (const auto& [name, value] : environment) {
        if (!validString(name) || !validString(value)) {
            done(std::unexpected(eError::INVALID_ARGUMENT));
            return;
        }

        names.emplace_back(name.c_str());
        values.emplace_back(value.c_str());
    }

    // malformed updates are a protocol error
    if (!Environment::validateUpdate(names, values)) {
        done(std::unexpected(eError::INVALID_ARGUMENT));
        return;
    }

    m_environmentResults.emplace_back(std::move(done));
    m_manager->sendUpdateTavernEnvironment(names, values);
}

std::expected<void, eError> CTavern::updateEnvironmentSync(const EnvironmentUpdate& environment, std::chrono::milliseconds timeout) {
    return await<void>(Clock::now() + timeout, [&](EnvironmentCallback&& done) { updateEnvironment(environment, std::move(done)); });
}
