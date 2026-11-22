#include "Manager.hpp"
#include "Arguments.hpp"
#include "BusObject.hpp"
#include "BusObjectHandle.hpp"
#include "BusQuery.hpp"
#include "CoreProtocol.hpp"
#include "PermissionContext.hpp"
#include "../../Tavern.hpp"
#include "../../bus/Registry.hpp"

#include <common/Environment.hpp>
#include <common/PermissionName.hpp>
#include <common/Wire.hpp>

CCoreManagerResource::CCoreManagerResource(SP<CHpHyprtavernCoreManagerV1Object> resource, SP<Core::CClient> client) : m_resource(std::move(resource)), m_client(client) {
    m_resource->setDestroy([this] { PROTO::core->destroyResource(this); });
    m_resource->setOnDestroy([this] { PROTO::core->destroyResource(this); });

    m_resource->setCreatePermissionContext([this](uint32_t seq, const char* token) { onCreatePermissionContext(seq, token); });

    m_resource->setGetBusObject([this](uint32_t seq, const char* name, const std::vector<const char*>& requirements) { onGetBusObject(seq, name, requirements); });

    m_resource->setGetQueryObject([this](uint32_t seq, const std::vector<const char*>& protocols, hpHyprtavernCoreV1BusQueryFilterMode protocolMode,
                                         const std::vector<const char*>&      properties,
                                         hpHyprtavernCoreV1BusQueryFilterMode propertyMode) { onGetQueryObject(seq, protocols, protocolMode, properties, propertyMode); });

    m_resource->setGetObjectHandle([this](uint32_t seq, uint32_t id) { onGetObjectHandle(seq, id); });

    m_resource->setUpdateTavernEnvironment([this](const std::vector<const char*>& names, const std::vector<const char*>& values) { onUpdateEnvironment(names, values); });
}

CCoreManagerResource::~CCoreManagerResource() {
    m_resource->setOnDestroy({});
}

void CCoreManagerResource::sendReady() {
    if (m_ready)
        return;

    m_ready = true;
    m_resource->sendReady();
}

bool CCoreManagerResource::ensureReady() {
    if (m_ready && m_client)
        return true;

    m_resource->error(Arguments::MALFORMED, "request sent before ready");
    return false;
}

void CCoreManagerResource::onCreatePermissionContext(uint32_t seq, const char* token) {
    if (!ensureReady())
        return;

    // even across multiple managers
    if (PROTO::core->hasPermissionContext(m_client->id())) {
        m_resource->error(Arguments::MALFORMED, "the client already has a permission context");
        return;
    }

    auto object = PROTO::core->createObject<CHpHyprtavernPermissionContextV1Object>(m_resource->getObject(), seq);
    if (!object)
        return;

    const auto CONTEXT = PROTO::core->m_contexts.emplace_back(makeShared<CPermissionContextResource>(std::move(object), m_client.lock()));
    CONTEXT->m_self    = CONTEXT;
    CONTEXT->begin(token);
}

void CCoreManagerResource::onGetBusObject(uint32_t seq, const char* name, const std::vector<const char*>& requirements) {
    if (!ensureReady())
        return;

    auto permissions = Arguments::permissions(requirements);
    if (!permissions) {
        m_resource->error(Arguments::MALFORMED, "invalid required permissions");
        return;
    }

    const auto CLIENT = m_client.lock();
    const auto ID     = Bus::registry->add(CLIENT->id(), CLIENT->principal().identity, name, std::move(*permissions));
    if (!ID) {
        m_resource->error(Arguments::MALFORMED, "too many bus objects");
        return;
    }

    auto object = PROTO::core->createObject<CHpHyprtavernBusObjectV1Object>(m_resource->getObject(), seq);
    if (!object) {
        Bus::registry->remove(*ID);
        return;
    }

    PROTO::core->m_busObjects.emplace_back(makeShared<CBusObjectResource>(std::move(object), *ID, CLIENT->role()));
}

void CCoreManagerResource::onGetQueryObject(uint32_t seq, const std::vector<const char*>& protocols, uint32_t protocolMode, const std::vector<const char*>& properties,
                                            uint32_t propertyMode) {
    if (!ensureReady())
        return;

    if (protocolMode > HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ANY || propertyMode > HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ANY || !Arguments::validArray(protocols) ||
        !Arguments::validArray(properties)) {
        m_resource->error(Arguments::MALFORMED, "invalid query");
        return;
    }

    Bus::SQuery query = {
        .protocols    = Wire::toStrings(protocols),
        .protocolMode = sc<Bus::eFilterMode>(protocolMode),
        .propertyMode = sc<Bus::eFilterMode>(propertyMode),
    };

    for (const auto& property : properties) {
        auto parsed = Bus::parseQueryProperty(property);
        if (!parsed) {
            m_resource->error(HP_HYPRTAVERN_CORE_V1_BUS_MANAGER_ERRORS_INVALID_PROPERTY, "invalid property in query");
            return;
        }

        query.properties.emplace_back(std::move(*parsed));
    }

    const auto CLIENT  = m_client.lock();
    auto       matches = Bus::registry->query(query, [&CLIENT](const Bus::SObject& object) { return CLIENT->canInspect(object); });

    auto       object = PROTO::core->createObject<CHpHyprtavernBusQueryV1Object>(m_resource->getObject(), seq);
    if (!object)
        return;

    PROTO::core->m_queries.emplace_back(makeShared<CBusQueryResource>(std::move(object), matches));
}

void CCoreManagerResource::onGetObjectHandle(uint32_t seq, uint32_t id) {
    if (!ensureReady())
        return;

    auto object = PROTO::core->createObject<CHpHyprtavernBusObjectHandleV1Object>(m_resource->getObject(), seq);
    if (!object)
        return;

    PROTO::core->m_handles.emplace_back(makeShared<CBusObjectHandleResource>(std::move(object), m_client.lock(), id));
}

void CCoreManagerResource::onUpdateEnvironment(const std::vector<const char*>& names, const std::vector<const char*>& values) {
    if (!ensureReady())
        return;

    if (const auto RET = Environment::validateUpdate(names, values); !RET) {
        m_resource->error(Arguments::MALFORMED, RET.error());
        return;
    }

    // environment can redirect or compromise barmaids, so only verified hosts may change it
    if (m_client->authorize(Security::PERMISSION_UNSANDBOXED) == Security::eAuthorization::DENIED) {
        m_resource->sendUpdateTavernEnvironmentResult(HP_HYPRTAVERN_CORE_V1_ENVIRONMENT_UPDATE_RESULT_DENIED);
        return;
    }

    g_tavern->updateEnvironment(names, values);
    m_resource->sendUpdateTavernEnvironmentResult(HP_HYPRTAVERN_CORE_V1_ENVIRONMENT_UPDATE_RESULT_APPLIED);
}
