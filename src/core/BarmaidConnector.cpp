#include "BarmaidConnector.hpp"

#include "../helpers/Logger.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <format>
#include <memory>
#include <string_view>
#include <thread>
#include <cstdint>

#include <poll.h>
#include <unistd.h>

namespace {
    using namespace std::chrono_literals;

    thread_local std::stop_token g_stopToken;

    enum class eWaitResult : uint8_t {
        READY,
        CANCELLED,
        TIMED_OUT,
        SOCKET_FAILED,
    };

    struct SQueryState {
        std::vector<uint32_t> results;
        bool                  answered = false;
    };

    struct SConnectState {
        ~SConnectState() {
            if (fd >= 0)
                close(fd);
        }

        int releaseFd() {
            const int result = fd;
            fd               = -1;
            return result;
        }

        int  fd     = -1;
        bool failed = false;
    };
}

template <typename Predicate>
static eWaitResult dispatchUntil(const SP<Hyprwire::IClientSocket>& sock, Predicate&& done, std::chrono::steady_clock::time_point deadline) {
    while (!done()) {
        if (g_stopToken.stop_requested())
            return eWaitResult::CANCELLED;

        const auto NOW = std::chrono::steady_clock::now();
        if (NOW >= deadline)
            return eWaitResult::TIMED_OUT;

        const auto REMAINING = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - NOW);
        const int  TIMEOUT   = static_cast<int>(std::clamp(REMAINING, 1ms, 100ms).count());
        pollfd     pfd{
            .fd      = sock->extractLoopFD(),
            .events  = POLLIN,
            .revents = 0,
        };

        const int ret = poll(&pfd, 1, TIMEOUT);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return eWaitResult::SOCKET_FAILED;
        }
        if (ret == 0)
            continue;

        if (pfd.revents & POLLIN) {
            if (!sock->dispatchEvents())
                return eWaitResult::SOCKET_FAILED;
            continue;
        }

        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
            return eWaitResult::SOCKET_FAILED;
    }

    return eWaitResult::READY;
}

static std::string waitError(eWaitResult result, std::string_view operation, std::string_view protocolName) {
    switch (result) {
        case eWaitResult::CANCELLED: return std::format("cancelled while {} {}", operation, protocolName);
        case eWaitResult::TIMED_OUT: return std::format("timed out while {} {}", operation, protocolName);
        case eWaitResult::SOCKET_FAILED: return std::format("internal socket failed while {} {}", operation, protocolName);
        case eWaitResult::READY: break;
    }
    return std::format("failed while {} {}", operation, protocolName);
}

void CBarmaidConnector::setThreadStopToken(std::stop_token stopToken) {
    g_stopToken = std::move(stopToken);
}

std::expected<int, std::string> CBarmaidConnector::connectToProtocol(SP<Hyprwire::IClientSocket> sock, SP<CCHpHyprtavernCoreManagerV1Object> manager, const char* protocolName,
                                                                     const std::vector<const char*>& extraProtocolScope) {
    if (!sock)
        return std::unexpected("missing internal socket");

    if (!manager)
        return std::unexpected("missing internal core manager");

    if (!protocolName)
        return std::unexpected("missing protocol name");

    std::vector<const char*> scope = {protocolName};

    for (const auto& protocol : extraProtocolScope) {
        if (!protocol)
            continue;

        const bool ALREADY_IN_SCOPE = std::ranges::any_of(scope, [protocol](const auto& scoped) { return std::string_view{scoped} == protocol; });
        if (!ALREADY_IN_SCOPE)
            scope.emplace_back(protocol);
    }

    constexpr auto        DISCOVERY_TIMEOUT  = std::chrono::seconds(5);
    constexpr auto        CONNECT_TIMEOUT    = std::chrono::seconds(5);
    const auto            DISCOVERY_DEADLINE = std::chrono::steady_clock::now() + DISCOVERY_TIMEOUT;

    std::vector<uint32_t> results;
    while (results.empty()) {
        if (g_stopToken.stop_requested())
            return std::unexpected(std::format("cancelled while discovering {}", protocolName));
        if (std::chrono::steady_clock::now() >= DISCOVERY_DEADLINE)
            return std::unexpected(std::format("bus has no object exposing {}", protocolName));

        auto state = std::make_shared<SQueryState>();
        auto query = makeShared<CCHpHyprtavernBusQueryV1Object>(
            manager->sendGetQueryObject(scope, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL, {}, HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL));

        query->setResults([state, protocolName](const std::vector<uint32_t>& res) {
            g_logger->log(LOG_DEBUG, "CBarmaidConnector: got {} results for {}", res.size(), protocolName);
            state->results  = res;
            state->answered = true;
        });

        const auto WAIT_RESULT = dispatchUntil(sock, [state] { return state->answered; }, DISCOVERY_DEADLINE);
        query->sendDestroy();
        if (WAIT_RESULT != eWaitResult::READY)
            return std::unexpected(waitError(WAIT_RESULT, "discovering", protocolName));

        results = std::move(state->results);
        if (results.empty()) {
            const auto SLEEP_UNTIL = std::min(DISCOVERY_DEADLINE, std::chrono::steady_clock::now() + 10ms);
            while (!g_stopToken.stop_requested() && std::chrono::steady_clock::now() < SLEEP_UNTIL)
                std::this_thread::sleep_for(1ms);
        }
    }

    auto handle = makeShared<CCHpHyprtavernBusObjectHandleV1Object>(manager->sendGetObjectHandle(results[0]));

    auto state = std::make_shared<SConnectState>();

    handle->setSocket([state](int connFd) {
        if (state->fd >= 0)
            close(connFd);
        else
            state->fd = connFd;
    });
    handle->setSocketFailed([state, protocolName](uint32_t err) {
        g_logger->log(LOG_ERR, "CBarmaidConnector: connect to {} failed with error {}", protocolName, err);
        state->failed = true;
    });
    handle->setFailed([state] { state->failed = true; });

    handle->sendConnect(scope);

    const auto WAIT_RESULT = dispatchUntil(sock, [state] { return state->fd >= 0 || state->failed; }, std::chrono::steady_clock::now() + CONNECT_TIMEOUT);
    if (WAIT_RESULT != eWaitResult::READY)
        return std::unexpected(waitError(WAIT_RESULT, "connecting to", protocolName));

    if (state->failed || state->fd < 0)
        return std::unexpected(std::format("failed to connect to {}", protocolName));

    return state->releaseFd();
}
