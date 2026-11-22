#include "BusQuery.hpp"
#include "CoreProtocol.hpp"

CBusQueryResource::CBusQueryResource(SP<CHpHyprtavernBusQueryV1Object> resource, const std::vector<uint32_t>& matches) : m_resource(std::move(resource)) {
    m_resource->setDestroy([this] { PROTO::core->destroyResource(this); });
    m_resource->setOnDestroy([this] { PROTO::core->destroyResource(this); });

    m_resource->sendResults(matches);
}

CBusQueryResource::~CBusQueryResource() {
    m_resource->setOnDestroy({});
}
