#include "KvClient.hpp"
#include "Tavern.hpp"

using namespace Hyprtavern;

namespace {
    constexpr uint32_t KV_PROTOCOL_VERSION = 1;
}

CKvClient::CKvClient(SP<Hyprwire::IClientSocket> socket) : m_socket(std::move(socket)) {
    ;
}

UP<CKvClient> CKvClient::connect(CTavern& tavern, Clock::time_point deadline) {
    auto       implementation = makeShared<CCHpHyprtavernKvStoreV1Impl>(KV_PROTOCOL_VERSION);
    const auto SPEC           = implementation->protocol();

    const auto SOCKET = tavern.connectUntil(SQuery{.protocols = {SPEC->specName()}}, {implementation}, deadline);
    if (!SOCKET)
        return nullptr;

    auto kv = makeUnique<CKvClient>(*SOCKET);

    // async, so store_available sent right after binding finds its listener
    (*SOCKET)->bindProtocolAsync(SPEC, KV_PROTOCOL_VERSION, [raw = kv.get()](SP<Hyprwire::IObject> object) {
        if (object)
            raw->bindManager(std::move(object));
    });

    if (!kv->waitUntil([raw = kv.get()] { return raw->m_available; }, deadline))
        return nullptr;

    return kv;
}

void CKvClient::bindManager(SP<Hyprwire::IObject> object) {
    m_manager = makeShared<CCHpHyprtavernKvStoreManagerV1Object>(std::move(object));

    m_manager->setStoreAvailable([this] { m_available = true; });
    m_manager->setValueObtained([this](const char*, const char* value, uint32_t) { m_result = std::string{value}; });
    m_manager->setValueStored([this](const char*, uint32_t) { m_result = std::string{}; });
    m_manager->setValueFailed([this](const char*, uint32_t, uint32_t reason) { m_result = std::unexpected(reason); });
}

std::expected<void, eError> CKvClient::waitUntil(const std::function<bool()>& done, Clock::time_point deadline) {
    return dispatchUntil(m_socket->extractLoopFD(), [this] { return m_socket->dispatchEvents(false); }, done, deadline);
}

std::expected<std::optional<std::string>, eError> CKvClient::get(const std::string& key, Clock::time_point deadline) {
    m_result.reset();
    m_manager->sendGetValue(key.c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE);

    if (const auto RET = waitUntil([this] { return m_result.has_value(); }, deadline); !RET)
        return std::unexpected(RET.error());

    if (*m_result)
        return std::optional{**m_result};

    // a failed lookup is not a missing value
    if (m_result->error() == HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_VALUE_MISSING)
        return std::nullopt;

    return std::unexpected(eError::UNKNOWN);
}

std::expected<void, eError> CKvClient::set(const std::string& key, const std::string& value, Clock::time_point deadline) {
    m_result.reset();
    m_manager->sendSetValue(key.c_str(), value.c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_APP_VALUE);

    if (const auto RET = waitUntil([this] { return m_result.has_value(); }, deadline); !RET)
        return std::unexpected(RET.error());

    // only value_stored confirms the write is durable
    if (!*m_result)
        return std::unexpected(eError::UNKNOWN);

    return {};
}
