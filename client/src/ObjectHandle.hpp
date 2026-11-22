#pragma once

#include <hyprtavern-client/Tavern.hpp>

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-client.hpp>

#include <hyprutils/os/FileDescriptor.hpp>

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace Hyprtavern {
    // a bus object handle: one snapshot of the object, then at most one connection attempt
    class CObjectHandle {
      public:
        using SnapshotFn  = std::function<void(CObjectHandle& handle, bool found)>;
        using ConnectedFn = std::function<void(std::expected<Hyprutils::OS::CFileDescriptor, eError>)>;

        CObjectHandle(const SP<CCHpHyprtavernCoreManagerV1Object>& manager, uint32_t id, SnapshotFn&& onSnapshot);

        // only from within onSnapshot, once the object was found
        void connect(const std::vector<std::string>& protocols, ConnectedFn&& onConnected);

        // whether the handle is inert, and its callbacks have returned
        bool        done() const;

        SObjectInfo m_info;

      private:
        void                                      handleSnapshot(bool found);
        void                                      handleConnection(std::expected<Hyprutils::OS::CFileDescriptor, eError> fd);
        void                                      retire();

        SP<CCHpHyprtavernBusObjectHandleV1Object> m_handle;
        SnapshotFn                                m_onSnapshot;
        ConnectedFn                               m_onConnected;
        bool                                      m_done = false;
    };
}
