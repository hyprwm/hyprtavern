#include "ObjectHandle.hpp"
#include "Helpers.hpp"

#include <common/Wire.hpp>

#include <string_view>

using namespace Hyprtavern;
using namespace Hyprutils::OS;

namespace {
    eError errorFromWire(uint32_t error) {
        switch (error) {
            case HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_BAD_PROTOCOL: return eError::BAD_PROTOCOL;
            case HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_CONNECTION_ERROR_INSUFFICIENT_PERMISSIONS: return eError::INSUFFICIENT_PERMISSIONS;
            default: return eError::UNKNOWN;
        }
    }
}

CObjectHandle::CObjectHandle(const SP<CCHpHyprtavernCoreManagerV1Object>& manager, uint32_t id, SnapshotFn&& onSnapshot) :
    m_handle(makeShared<CCHpHyprtavernBusObjectHandleV1Object>(manager->sendGetObjectHandle(id))), m_onSnapshot(std::move(onSnapshot)) {
    m_info.id = id;

    m_handle->setName([this](const char* name) { m_info.name = name; });

    m_handle->setRequiredPermissions([this](const std::vector<const char*>& permissions) { m_info.requiredPermissions = Wire::toStrings(permissions); });

    m_handle->setProtocol([this](const char* name, uint32_t version, const std::vector<const char*>& permissions) {
        m_info.protocols[name] = {.version = version, .requiredPermissions = Wire::toStrings(permissions)};
    });

    m_handle->setProperties([this](const std::vector<const char*>& properties) {
        for (const std::string_view property : properties) {
            const auto EQUALS = property.find('=');
            if (EQUALS != std::string_view::npos)
                m_info.properties.emplace(property.substr(0, EQUALS), property.substr(EQUALS + 1));
        }
    });

    m_handle->setProviderIdentity([this](const char* id, uint32_t kind, uint32_t stable) { m_info.provider = identityFromWire(id, kind, stable); });

    m_handle->setFailed([this] { handleSnapshot(false); });
    m_handle->setDone([this] { handleSnapshot(true); });

    m_handle->setSocket([this](int fd) { handleConnection(CFileDescriptor{fd}); });
    m_handle->setSocketFailed([this](uint32_t error) { handleConnection(std::unexpected(errorFromWire(error))); });
}

void CObjectHandle::connect(const std::vector<std::string>& protocols, ConnectedFn&& onConnected) {
    m_onConnected = std::move(onConnected);
    m_handle->sendConnect(Wire::toCStrings(protocols));
}

bool CObjectHandle::done() const {
    return m_done;
}

void CObjectHandle::handleSnapshot(bool found) {
    m_onSnapshot(*this, found);

    // a connection attempt keeps the handle until it's settled
    if (!m_onConnected)
        retire();
}

void CObjectHandle::handleConnection(std::expected<CFileDescriptor, eError> fd) {
    if (m_onConnected)
        m_onConnected(std::move(fd));

    retire();
}

void CObjectHandle::retire() {
    m_handle->sendDestroy();

    // last, so the handle is never dropped while its callbacks run
    m_done = true;
}
