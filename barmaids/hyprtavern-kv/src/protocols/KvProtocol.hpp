#pragma once

#include "../core/Core.hpp"

#include <common/Memory.hpp>

#include <hp_hyprtavern_kv_store_v1-server.hpp>

#include <expected>
#include <string>
#include <vector>

class CKvManagerResource {
  public:
    CKvManagerResource(SP<CHpHyprtavernKvStoreManagerV1Object> resource, SAccess access);
    ~CKvManagerResource();

  private:
    void onSetValue(const char* key, const char* value, uint32_t type);
    void onGetValue(const char* key, uint32_t type);

    // the store scope for a value type, or why the connection may not use it
    std::expected<std::string, uint32_t>    scopeFor(uint32_t type) const;
    bool                                    validateRequest(const char* key, uint32_t type);

    SP<CHpHyprtavernKvStoreManagerV1Object> m_resource;
    SAccess                                 m_access;
};

class CKvProtocol {
  public:
    CKvProtocol(const SP<Hyprwire::IServerSocket>& socket);

    // for serving the store on another socket too
    SP<CHpHyprtavernKvStoreV1Impl> implementation();

  private:
    void                                bindManager(SP<Hyprwire::IObject> object);
    void                                destroyResource(CKvManagerResource* resource);

    std::vector<SP<CKvManagerResource>> m_managers;

    friend class CKvManagerResource;
};

namespace PROTO {
    inline UP<CKvProtocol> kv;
}
