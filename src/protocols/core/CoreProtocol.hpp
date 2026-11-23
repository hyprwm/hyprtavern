#pragma once

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-server.hpp>

#include <unordered_map>

class CCoreManagerResource;
class CBusObjectResource;
class CBusQueryResource;
class CBusObjectHandleResource;
class CPermissionContextResource;
class CConnectionPrincipalResource;

class CCoreProtocol {
  public:
    CCoreProtocol(const SP<Hyprwire::IServerSocket>& socket);

    // managers receive ready once the tavern can serve their requests
    void                   setReady();
    bool                   ready() const;

    SP<CBusObjectResource> busObject(uint32_t id) const;

    // a client connection may have at most one live permission context
    bool hasPermissionContext(uint64_t clientID) const;

  private:
    // creates the wire object for a request returning one. Null on failure, which errors the client.
    template <typename T>
    SP<T> createObject(const SP<Hyprwire::IObject>& parent, uint32_t seq) {
        const auto SOCKET = m_socket.lock();
        if (!SOCKET)
            return nullptr;

        auto object = SOCKET->createObject(parent->client(), parent, T::name(), seq);
        if (!object)
            return nullptr;

        return makeShared<T>(std::move(object));
    }

    // keeps a resource alive until it's destroyed
    template <typename T>
    SP<T> keep(std::unordered_map<T*, SP<T>>& resources, SP<T> resource) {
        resources.emplace(resource.get(), resource);
        return resource;
    }

    void                                                                                bindManager(SP<Hyprwire::IObject> object);

    void                                                                                destroyResource(CCoreManagerResource* resource);
    void                                                                                destroyResource(CBusObjectResource* resource);
    void                                                                                destroyResource(CBusQueryResource* resource);
    void                                                                                destroyResource(CBusObjectHandleResource* resource);
    void                                                                                destroyResource(CPermissionContextResource* resource);
    void                                                                                destroyResource(CConnectionPrincipalResource* resource);

    std::unordered_map<CCoreManagerResource*, SP<CCoreManagerResource>>                 m_managers;
    std::unordered_map<CBusQueryResource*, SP<CBusQueryResource>>                       m_queries;
    std::unordered_map<CBusObjectHandleResource*, SP<CBusObjectHandleResource>>         m_handles;
    std::unordered_map<CConnectionPrincipalResource*, SP<CConnectionPrincipalResource>> m_principals;

    // by bus object id, and by client id
    std::unordered_map<uint32_t, SP<CBusObjectResource>>         m_busObjects;
    std::unordered_map<uint64_t, SP<CPermissionContextResource>> m_contexts;

    WP<Hyprwire::IServerSocket>                                  m_socket;
    bool                                                         m_ready = false;

    friend class CCoreManagerResource;
    friend class CBusObjectResource;
    friend class CBusQueryResource;
    friend class CBusObjectHandleResource;
    friend class CPermissionContextResource;
    friend class CConnectionPrincipalResource;
};

namespace PROTO {
    inline UP<CCoreProtocol> core;
}
