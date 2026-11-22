#include "KvProtocol.hpp"

#include <common/Log.hpp>

#include <algorithm>

constexpr uint32_t KV_STORE_PROTOCOL_VERSION = 1;

CKvManagerResource::CKvManagerResource(SP<CHpHyprtavernKvStoreManagerV1Object> resource, SAccess access) : m_resource(std::move(resource)), m_access(std::move(access)) {
    m_resource->setOnDestroy([this] { PROTO::kv->destroyResource(this); });

    m_resource->setSetValue([this](const char* key, const char* value, hpHyprtavernKvStoreV1ValueType type) { onSetValue(key, value, type); });
    m_resource->setGetValue([this](const char* key, hpHyprtavernKvStoreV1ValueType type) { onGetValue(key, type); });

    // the store is loaded on startup, so it's always available
    m_resource->setRequestStoreStatus([this] { m_resource->sendStoreAvailable(); });
    m_resource->sendStoreAvailable();
}

CKvManagerResource::~CKvManagerResource() {
    m_resource->setOnDestroy({});
}

std::expected<std::string, uint32_t> CKvManagerResource::scopeFor(uint32_t type) const {
    switch (type) {
        case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE:
            if (m_access.appScope.empty())
                return std::unexpected(HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_IDENTITY_UNAVAILABLE);

            return "app:" + m_access.appScope;
        case HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_UNBOUNDED_VALUE: return "unbounded";
        default:
            // ordinary permission bypass does not grant this
            if (!m_access.tavern)
                return std::unexpected(HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_PERMISSION_DENIED);

            return "tavern";
    }
}

bool CKvManagerResource::validateRequest(const char* key, uint32_t type) {
    if (!Kv::isValidKey(key)) {
        m_resource->error(HP_HYPRTAVERN_KV_STORE_V1_PROTOCOL_ERRORS_INVALID_KEY_NAME, "invalid key name");
        return false;
    }

    if (type > HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE) {
        m_resource->error(UINT32_MAX, "invalid value type");
        return false;
    }

    return true;
}

void CKvManagerResource::onSetValue(const char* key, const char* value, uint32_t type) {
    if (!validateRequest(key, type))
        return;

    const auto SCOPE = scopeFor(type);
    if (!SCOPE) {
        m_resource->sendValueFailed(key, type, SCOPE.error());
        return;
    }

    if (const auto RET = g_core->m_store->set(*SCOPE, key, value); !RET) {
        g_logger->log(LOG_ERR, "failed to store {}: {}", key, RET.error());
        m_resource->sendValueFailed(key, type, HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_UNKNOWN_ERROR);
        return;
    }

    m_resource->sendValueStored(key, type);
}

void CKvManagerResource::onGetValue(const char* key, uint32_t type) {
    if (!validateRequest(key, type))
        return;

    const auto SCOPE = scopeFor(type);
    if (!SCOPE) {
        m_resource->sendValueFailed(key, type, SCOPE.error());
        return;
    }

    const auto VALUE = g_core->m_store->get(*SCOPE, key);
    if (!VALUE) {
        m_resource->sendValueFailed(key, type, HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_VALUE_MISSING);
        return;
    }

    m_resource->sendValueObtained(key, VALUE->c_str(), type);
}

CKvProtocol::CKvProtocol(const SP<Hyprwire::IServerSocket>& socket) {
    socket->addImplementation(makeShared<CHpHyprtavernKvStoreV1Impl>(KV_STORE_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> object) { bindManager(std::move(object)); }));
}

void CKvProtocol::bindManager(SP<Hyprwire::IObject> object) {
    auto       resource = makeShared<CHpHyprtavernKvStoreManagerV1Object>(std::move(object));
    const auto ACCESS   = g_core->access(resource->getObject()->client());

    // every client is registered with its access when added
    if (!ACCESS) {
        resource->error(UINT32_MAX, "unknown client");
        return;
    }

    m_managers.emplace_back(makeShared<CKvManagerResource>(std::move(resource), *ACCESS));
}

void CKvProtocol::destroyResource(CKvManagerResource* resource) {
    std::erase_if(m_managers, [resource](const auto& other) { return other.get() == resource; });
}
