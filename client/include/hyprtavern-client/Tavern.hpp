#pragma once

#include "Types.hpp"

#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprwire/hyprwire.hpp>

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Hyprtavern {
    struct STavernOptions {
        // self-asserted, shown when the user is asked for permissions
        std::string appName, appDescription;

        // keep the permission context's token in the app's kv values, so permanent grants are restored on the next run
        bool persistPermissions = true;

        // an already connected tavern socket, of which the tavern takes ownership. Defaults to $XDG_RUNTIME_DIR/hyprtavern/ht.sock
        std::optional<int> fd;
    };

    /*
        Matches objects exposing the given protocols and properties. Empty filters impose no
        constraint, and only objects visible to this client are returned.
    */
    struct SQuery {
        std::vector<std::string>                         protocols;
        eFilterMode                                      protocolMode = eFilterMode::ALL;
        std::vector<std::pair<std::string, std::string>> properties;
        eFilterMode                                      propertyMode = eFilterMode::ALL;
    };

    // a snapshot of a bus object. Names and properties are provider-supplied, the provider identity is attested.
    struct SObjectInfo {
        uint32_t                           id = 0;
        std::string                        name;
        std::map<std::string, uint32_t>    protocols;
        std::map<std::string, std::string> properties;
        SIdentity                          provider;
    };

    struct SPermissionResult {
        ePermissionResult result = ePermissionResult::FAILED;

        // the actual lifetime of a granted permission
        bool permanent = false;
    };

    using ClientImplementations = std::vector<Hyprutils::Memory::CSharedPointer<Hyprwire::IProtocolClientImplementation>>;
    using EnvironmentUpdate     = std::vector<std::pair<std::string, std::string>>;
    using PermissionResults     = std::unordered_map<std::string, SPermissionResult>;

    using PermissionCallback  = std::function<void(PermissionResults)>;
    using FindCallback        = std::function<void(std::expected<std::vector<SObjectInfo>, eError>)>;
    using ConnectCallback     = std::function<void(std::expected<Hyprutils::Memory::CSharedPointer<Hyprwire::IClientSocket>, eError>)>;
    using EnvironmentCallback = std::function<void(std::expected<void, eError>)>;

    /*
        A connection to the tavern.

        Callbacks run from dispatch(), or synchronously for invalid input. The *Sync variants
        dispatch until they are done, so prefer the async ones from within callbacks.

        Not thread-safe. Do not drop the tavern from within its own callbacks.
    */
    class HYPRTAVERN_API ITavern {
      public:
        virtual ~ITavern() = default;

        // connects, and waits until the tavern is ready to serve
        static std::expected<Hyprutils::Memory::CSharedPointer<ITavern>, eError> open(const STavernOptions& options = {}, std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

        // poll fd(), and call dispatch() when it's readable. Returns false once the tavern is gone.
        virtual int  fd()       = 0;
        virtual bool dispatch() = 0;

        /*
            Request permissions, which may prompt the user. Every name gets a result, and a request for
            a name that is already pending joins it. PERMANENT may still yield a session grant.

            Request every permission you need: a policy may grant them, but nothing guarantees one.
            If the tavern can't provide this connection a permission context, every request fails.
        */
        virtual void                                     requestPermissions(const std::vector<std::string>& permissions, ePermissionMode mode, PermissionCallback&& done) = 0;
        virtual std::expected<PermissionResults, eError> requestPermissionsSync(const std::vector<std::string>& permissions, ePermissionMode mode,
                                                                                std::chrono::milliseconds timeout = PROMPT_TIMEOUT)                                       = 0;

        // snapshots of all matching objects. Objects may change or disappear right after.
        virtual void                                            find(const SQuery& query, FindCallback&& done)                                     = 0;
        virtual std::expected<std::vector<SObjectInfo>, eError> findSync(const SQuery& query, std::chrono::milliseconds timeout = DEFAULT_TIMEOUT) = 0;

        /*
            Connect to an object on the protocols of the given implementations, which are added to
            the returned socket. The handshake is done, binding is left to you.

            Connecting never prompts: request the required permissions beforehand.
        */
        virtual void connect(uint32_t objectID, const ClientImplementations& implementations, ConnectCallback&& done)                                              = 0;
        virtual std::expected<Hyprutils::Memory::CSharedPointer<Hyprwire::IClientSocket>, eError> connectSync(uint32_t objectID, const ClientImplementations& implementations,
                                                                                                              std::chrono::milliseconds timeout = DEFAULT_TIMEOUT) = 0;

        // connects to the first matching object that exposes all the implementations' protocols
        virtual std::expected<Hyprutils::Memory::CSharedPointer<Hyprwire::IClientSocket>, eError> connectSync(const SQuery& query, const ClientImplementations& implementations,
                                                                                                              std::chrono::milliseconds timeout = DEFAULT_TIMEOUT) = 0;

        /*
            Update the environment of the tavern and its barmaids, e.g. WAYLAND_DISPLAY. An empty value unsets
            a variable. Only verified, unsandboxed host applications may do this, others get DENIED.
        */
        virtual void                        updateEnvironment(const EnvironmentUpdate& environment, EnvironmentCallback&& done)                              = 0;
        virtual std::expected<void, eError> updateEnvironmentSync(const EnvironmentUpdate& environment, std::chrono::milliseconds timeout = DEFAULT_TIMEOUT) = 0;
    };
}
