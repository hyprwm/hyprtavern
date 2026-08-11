#pragma once

#include <hp_hyprtavern_core_v1-client.hpp>

#include "../helpers/Memory.hpp"

#include <expected>
#include <stop_token>
#include <string>
#include <vector>

class CBarmaidConnector {
  public:
    static std::expected<int, std::string> connectToProtocol(SP<Hyprwire::IClientSocket> sock, SP<CCHpHyprtavernCoreManagerV1Object> manager, const char* protocolName,
                                                             const std::vector<const char*>& extraProtocolScope = {});

    // Applies to connector waits performed by the calling thread.
    static void setThreadStopToken(std::stop_token stopToken);
};
