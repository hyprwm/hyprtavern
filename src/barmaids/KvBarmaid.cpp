#include "KvBarmaid.hpp"

#include <common/Log.hpp>

using namespace Barmaids;

constexpr uint32_t KV_STORE_PROTOCOL_VERSION = 1;

CKvBarmaid::CKvBarmaid() : CBarmaid("hyprtavern-kv", Bus::eProviderRole::KV_STORE, true) {
    ;
}

void CKvBarmaid::onConnected() {
    auto       impl = makeShared<CCHpHyprtavernKvStoreV1Impl>(KV_STORE_PROTOCOL_VERSION);
    const auto SPEC = impl->protocol();
    m_control->addImplementation(std::move(impl));

    m_control->bindProtocolAsync(SPEC, KV_STORE_PROTOCOL_VERSION, [this](SP<Hyprwire::IObject> object) {
        if (!object) {
            g_logger->log(LOG_ERR, "the kv barmaid does not provide a store, persistence is unavailable");
            return;
        }

        m_manager = makeShared<CCHpHyprtavernKvStoreManagerV1Object>(std::move(object));

        m_manager->setStoreAvailable([this] { onAvailable(); });

        m_manager->setValueObtained([this](const char* keyName, const char* value, uint32_t type) {
            const std::string KEY     = keyName;
            auto              request = finish(KEY, type);
            if (!request)
                return;

            if (request->onGet)
                request->onGet(std::string{value});
            else
                request->onSet(false);

            pump(KEY);
        });

        m_manager->setValueFailed([this](const char* keyName, uint32_t type, uint32_t reason) {
            const std::string KEY     = keyName;
            auto              request = finish(KEY, type);
            if (!request)
                return;

            if (request->onGet)
                request->onGet(std::unexpected(reason == HP_HYPRTAVERN_KV_STORE_V1_VALUE_OBTAINING_ERROR_VALUE_MISSING ? eError::MISSING : eError::FAILED));
            else
                request->onSet(false);

            pump(KEY);
        });

        m_manager->setValueStored([this](const char* keyName, uint32_t type) {
            const std::string KEY     = keyName;
            auto              request = finish(KEY, type);
            if (!request)
                return;

            if (request->onSet)
                request->onSet(true);
            else
                request->onGet(std::unexpected(eError::FAILED));

            pump(KEY);
        });
    });
}

bool CKvBarmaid::available() const {
    return m_available;
}

void CKvBarmaid::onAvailable() {
    // also answers status requests
    if (m_available)
        return;

    g_logger->log(LOG_DEBUG, "the kv store is available");

    m_available = true;

    std::vector<std::string> keys;
    for (const auto& [key, queue] : m_queues) {
        keys.emplace_back(key);
    }

    for (const auto& key : keys) {
        pump(key);
    }

    m_storeEvents.available.emit();
}

void CKvBarmaid::get(const std::string& key, GetCallback&& callback) {
    m_queues[key].requests.emplace_back(SRequest{.onGet = std::move(callback)});
    pump(key);
}

void CKvBarmaid::set(const std::string& key, ValueFn&& value, SetCallback&& callback) {
    m_queues[key].requests.emplace_back(SRequest{.value = std::move(value), .onSet = std::move(callback)});
    pump(key);
}

void CKvBarmaid::pump(const std::string& key) {
    const auto IT = m_queues.find(key);
    if (IT == m_queues.end())
        return;

    auto& queue = IT->second;

    // the store allows only one pending request per key
    while (m_available && m_manager && !queue.inFlight && !queue.requests.empty()) {
        queue.inFlight = true;

        auto& request = queue.requests.front();

        if (!request.value) {
            m_manager->sendGetValue(key.c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
            break;
        }

        // in flight while producing, so writes queued by the producer wait their turn
        const auto VALUE = (*request.value)();
        if (!VALUE) {
            queue.requests.pop_front();
            queue.inFlight = false;
            continue;
        }

        m_manager->sendSetValue(key.c_str(), VALUE->c_str(), HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE);
    }

    if (!queue.inFlight && queue.requests.empty())
        m_queues.erase(IT);
}

std::optional<CKvBarmaid::SRequest> CKvBarmaid::finish(const std::string& key, uint32_t type) {
    const auto IT = m_queues.find(key);
    if (type != HP_HYPRTAVERN_KV_STORE_V1_VALUE_TYPE_TAVERN_VALUE || IT == m_queues.end() || !IT->second.inFlight) {
        g_logger->log(LOG_WARN, "the kv barmaid answered a request that was never made, for {}", key);
        return std::nullopt;
    }

    auto request = std::move(IT->second.requests.front());
    IT->second.requests.pop_front();
    IT->second.inFlight = false;

    return request;
}
