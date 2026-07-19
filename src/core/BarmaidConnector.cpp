#include "BarmaidConnector.hpp"

#include "../helpers/Logger.hpp"

#include <chrono>
#include <format>
#include <thread>

std::expected<int, std::string> CBarmaidConnector::connectToProtocol(SP<Hyprwire::IClientSocket> sock, SP<CCHpHyprtavernCoreManagerV1Object> manager, const char* protocolName,
                                                                     const std::vector<const char*>& extraProtocolScope) {
    if (!sock)
        return std::unexpected("missing internal socket");

    if (!manager)
        return std::unexpected("missing internal core manager");

    std::vector<const char*> scope = {protocolName};

    for (const auto& p : extraProtocolScope) {
        bool alreadyInScope = false;
        for (const auto& scoped : scope) {
            if (std::string_view{scoped} == p) {
                alreadyInScope = true;
                break;
            }
        }

        if (!alreadyInScope)
            scope.emplace_back(p);
    }

    std::vector<uint32_t> results;

    for (size_t attempt = 0; attempt < 100; ++attempt) {
        results.clear();

        auto query = makeShared<CCHpHyprtavernBusQueryV1Object>(
            manager->sendGetQueryObject(scope, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL, {}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL));

        query->setResults([&results, protocolName](const std::vector<uint32_t>& res) {
            g_logger->log(LOG_DEBUG, "CBarmaidConnector: got {} results for {}", res.size(), protocolName);
            results = res;
        });

        sock->roundtrip();
        query->sendDestroy();

        if (!results.empty())
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (results.empty())
        return std::unexpected(std::format("bus has no object exposing {}", protocolName));

    auto handle = makeShared<CCHpHyprtavernBusObjectHandleV1Object>(manager->sendGetObjectHandle(results[0]));

    int  fd     = -1;
    bool failed = false;

    handle->setSocket([&fd](int connFd) { fd = connFd; });
    handle->setSocketFailed([&failed, protocolName](uint32_t err) {
        g_logger->log(LOG_ERR, "CBarmaidConnector: connect to {} failed with error {}", protocolName, err);
        failed = true;
    });
    handle->setFailed([&failed] { failed = true; });

    handle->sendConnect(scope);

    while (fd < 0 && !failed)
        sock->roundtrip();

    if (failed || fd < 0)
        return std::unexpected(std::format("failed to connect to {}", protocolName));

    return fd;
}
