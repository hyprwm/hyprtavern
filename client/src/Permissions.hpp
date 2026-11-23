#pragma once

#include <hyprtavern-client/Tavern.hpp>

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-client.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Hyprtavern {
    // the connection's permission context, created on the first request. Permanent grants are restored by the tavern.
    class CPermissions {
      public:
        CPermissions(SP<CCHpHyprtavernCoreManagerV1Object> manager, const STavernOptions& options);

        void request(const std::vector<std::string>& permissions, ePermissionMode mode, PermissionCallback&& done);
        void sweep();

      private:
        // one request, waiting for the results of all its permissions
        struct SBatch {
            PermissionResults  results;
            size_t             pending = 0;
            PermissionCallback callback;
            bool               done = false;

            void               settle(const std::string& permission, SPermissionResult result);
            void               release();
        };

        void                                                  ensureContext();
        void                                                  onResult(const std::string& permission, uint32_t result, uint32_t mode);
        void                                                  onUnavailable();

        SP<CCHpHyprtavernCoreManagerV1Object>                 m_manager;
        SP<CCHpHyprtavernPermissionContextV1Object>           m_context;
        std::string                                           m_appName, m_appDescription;

        bool                                                  m_unavailable = false;

        std::unordered_map<std::string, std::vector<SBatch*>> m_pending;
        std::vector<UP<SBatch>>                               m_batches;
    };
}
