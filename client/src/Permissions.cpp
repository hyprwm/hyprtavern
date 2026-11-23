#include "Permissions.hpp"

#include <common/PermissionName.hpp>

#include <algorithm>
#include <unordered_set>
#include <utility>

using namespace Hyprtavern;

namespace {
    SPermissionResult resultFromWire(uint32_t result, uint32_t mode) {
        switch (result) {
            case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED:
            case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_ALREADY_GRANTED:
            case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_GRANTED_BY_POLICY:
                return {.result = ePermissionResult::GRANTED, .permanent = mode == HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_MODE_PERMANENT};
            case HP_HYPRTAVERN_CORE_V1_SECURITY_PERMISSION_RESULT_DENIED: return {.result = ePermissionResult::DENIED};
            default: return {.result = ePermissionResult::FAILED};
        }
    }
}

void CPermissions::SBatch::settle(const std::string& permission, SPermissionResult result) {
    results[permission] = result;
    release();
}

void CPermissions::SBatch::release() {
    if (--pending > 0)
        return;

    callback(std::move(results));

    // last, so the batch is never dropped while its callback runs
    done = true;
}

CPermissions::CPermissions(SP<CCHpHyprtavernCoreManagerV1Object> manager, const STavernOptions& options) :
    m_manager(std::move(manager)), m_appName(options.appName), m_appDescription(options.appDescription) {
    ;
}

void CPermissions::ensureContext() {
    if (m_context)
        return;

    m_context = makeShared<CCHpHyprtavernPermissionContextV1Object>(m_manager->sendCreatePermissionContext());

    m_context->setUnavailable([this] { onUnavailable(); });
    m_context->setPermissionResult([this](const char* permission, uint32_t result, uint32_t mode) { onResult(permission, result, mode); });

    if (!m_appName.empty() || !m_appDescription.empty())
        m_context->sendSetDisplayIdentity(m_appName.c_str(), m_appDescription.c_str());
}

void CPermissions::request(const std::vector<std::string>& permissions, ePermissionMode mode, PermissionCallback&& done) {
    ensureContext();

    const std::unordered_set<std::string> UNIQUE{permissions.begin(), permissions.end()};

    auto                                  batch = m_batches.emplace_back(makeUnique<SBatch>()).get();
    batch->callback                             = std::move(done);

    // held until everything is sent, so it can't finish halfway through
    batch->pending = UNIQUE.size() + 1;

    for (const auto& permission : UNIQUE) {
        // invalid names are a protocol error, so they never reach the tavern
        if (m_unavailable || !Security::isRequestablePermission(permission)) {
            batch->settle(permission, {.result = ePermissionResult::FAILED});
            continue;
        }

        auto& waiting = m_pending[permission];

        // at most one request per name may be pending
        if (waiting.empty())
            m_context->sendRequestPermission(permission.c_str(), sc<hpHyprtavernCoreV1SecurityPermissionMode>(mode));

        waiting.emplace_back(batch);
    }

    batch->release();
}

void CPermissions::sweep() {
    // batches are only done once their callbacks returned, so none of these is running
    std::erase_if(m_batches, [](const auto& batch) { return batch->done; });
}

void CPermissions::onResult(const std::string& permission, uint32_t result, uint32_t mode) {
    const auto IT = m_pending.find(permission);
    if (IT == m_pending.end())
        return;

    const auto WAITING = std::move(IT->second);
    m_pending.erase(IT);

    const auto RESULT = resultFromWire(result, mode);

    for (const auto batch : WAITING) {
        batch->settle(permission, RESULT);
    }
}

void CPermissions::onUnavailable() {
    m_unavailable = true;

    // the context is inert, pending requests get no results anymore
    for (const auto& [permission, waiting] : std::exchange(m_pending, {})) {
        for (const auto batch : waiting) {
            batch->settle(permission, {.result = ePermissionResult::FAILED});
        }
    }
}
