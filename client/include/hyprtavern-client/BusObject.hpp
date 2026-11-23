#pragma once

#include "Types.hpp"

#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/signal/Signal.hpp>
#include <hyprwire/hyprwire.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace Hyprtavern {
    // values match bus_object_expose_errors in hp_hyprtavern_core_v1
    enum class eExposeError : uint8_t {
        ALREADY_EXPOSED       = 0,
        UNAUTHORIZED_PROVIDER = 1,
        INVALID_REGISTRATION  = 2,
    };

    struct SExposeOptions {
        // served on routed connections. The protocol name comes from its spec.
        Hyprutils::Memory::CSharedPointer<Hyprwire::IProtocolServerImplementation> implementation;

        // the max revision to advertise, 0 for the implementation's own
        uint32_t version = 0;

        // needed to connect on this protocol, in addition to the object's requirements
        std::vector<std::string> requiredPermissions;

        // no other object may expose this protocol. Only unsandboxed host apps may do this.
        bool exclusive = false;
    };

    /*
        Who a routed connection was admitted for, as attested by the tavern. This does not change
        during the connection, later grants or revocations only affect new connections.
    */
    struct HYPRTAVERN_API SPrincipal {
        SIdentity identity;

        // established by the tavern itself
        bool internal = false;

        // ordinary permission checks are bypassed by policy
        bool bypassOrdinary = false;

        // observed at admission, never use it for authorization
        uint32_t pid = 0;

        // self-asserted, never use them for authorization
        std::string              displayName, displayDescription;

        std::vector<std::string> grants;

        // the protocols allowed on this connection
        std::vector<std::string> scope;

        // whether a permission was held at admission, by grant, catch-all or policy
        bool has(std::string_view permission) const;
    };

    /*
        An object on the bus, serving the protocols it exposes to clients the tavern routes to it.
        Dropping it removes it from the bus, and closes its connections.

        Not thread-safe. Do not drop the object from within its own callbacks.
    */
    class HYPRTAVERN_API IBusObject {
      public:
        virtual ~IBusObject() = default;

        /*
            Expose a protocol. Exposing one again replaces its registration, for new connections.
            Refusals are emitted through m_events.exposeFailed, exposeSync waits for the verdict.
        */
        virtual void                              expose(const SExposeOptions& options)     = 0;
        virtual std::expected<void, eExposeError> exposeSync(const SExposeOptions& options) = 0;

        // an empty value removes a property. Returns false for invalid or overlong names or values.
        virtual bool setProperty(const std::string& name, const std::string& value) = 0;

        // replaces the permissions needed to discover or connect to this object. Returns false for invalid names.
        virtual bool requirePermissions(const std::vector<std::string>& permissions) = 0;

        // routed connections: poll fd(), and call dispatch() when it's readable
        virtual int  fd()       = 0;
        virtual bool dispatch() = 0;

        // the principal of a routed client, e.g. from IObject::client() in a bind handler. Null for unknown clients.
        virtual const SPrincipal* principal(const Hyprutils::Memory::CSharedPointer<Hyprwire::IServerClient>& client) const = 0;

        struct {
            // the protocol name, and why the tavern refused it
            Hyprutils::Signal::CSignalT<std::string, eExposeError> exposeFailed;
        } m_events;
    };
}
