#include "BusObject.hpp"
#include "Arguments.hpp"
#include "CoreProtocol.hpp"

#include <common/Wire.hpp>

namespace {
    uint32_t toWire(Bus::eRegistryError error) {
        switch (error) {
            case Bus::eRegistryError::ALREADY_EXPOSED: return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_EXPOSE_ERRORS_ALREADY_EXPOSED;
            case Bus::eRegistryError::UNAUTHORIZED_PROVIDER: return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_EXPOSE_ERRORS_UNAUTHORIZED_PROVIDER;
            default: return HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_EXPOSE_ERRORS_INVALID_REGISTRATION;
        }
    }
}

CBusObjectResource::CBusObjectResource(SP<CHpHyprtavernBusObjectV1Object> resource, uint32_t id, Bus::eProviderRole role) :
    m_resource(std::move(resource)), m_id(id), m_role(role) {
    m_resource->setDestroy([this] { PROTO::core->destroyResource(this); });
    m_resource->setOnDestroy([this] { PROTO::core->destroyResource(this); });

    m_resource->setExposeProtocol([this](const char* name, uint32_t version, const std::vector<const char*>& requirements, uint32_t exclusive) {
        auto permissions = Arguments::permissions(requirements);
        if (!permissions) {
            m_resource->error(Arguments::MALFORMED, "invalid required permissions");
            return;
        }

        const auto RET = Bus::registry->exposeProtocol(m_id, m_role, {.name = name, .version = version, .requirements = std::move(*permissions), .exclusive = exclusive != 0});
        if (RET)
            return;

        if (RET.error() == Bus::eRegistryError::LIMIT_REACHED) {
            m_resource->error(Arguments::MALFORMED, "too many protocols");
            return;
        }

        m_resource->sendExposeProtocolError(toWire(RET.error()), name);
    });

    m_resource->setExposeProperty([this](const char* name, const char* value) {
        if (!Bus::isValidPropertyName(name)) {
            m_resource->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "invalid property name");
            return;
        }

        if (!Bus::registry->exposeProperty(m_id, name, value))
            m_resource->error(Arguments::MALFORMED, "too many properties");
    });

    m_resource->setRequirePermissions([this](const std::vector<const char*>& requirements) {
        auto permissions = Arguments::permissions(requirements);
        if (!permissions) {
            m_resource->error(Arguments::MALFORMED, "invalid required permissions");
            return;
        }

        Bus::registry->setRequirements(m_id, std::move(*permissions));
    });
}

CBusObjectResource::~CBusObjectResource() {
    m_resource->setOnDestroy({});

    if (Bus::registry)
        Bus::registry->remove(m_id);
}

uint32_t CBusObjectResource::id() const {
    return m_id;
}

void CBusObjectResource::sendNewFd(int fd, const std::string& token, const std::vector<std::string>& scope) {
    m_resource->sendNewFd(fd, token.c_str(), Wire::toCStrings(scope));
}
