#include <hyprtavern-client/Types.hpp>

std::string_view Hyprtavern::errorString(eError error) {
    switch (error) {
        case eError::NO_TAVERN: return "the tavern is not serving beer";
        case eError::HANDSHAKE_FAILED: return "handshake failed";
        case eError::UNSUPPORTED: return "the tavern does not support this client";
        case eError::TIMED_OUT: return "timed out";
        case eError::DISCONNECTED: return "disconnected";
        case eError::INVALID_ARGUMENT: return "invalid argument";
        case eError::NOT_FOUND: return "no such object";
        case eError::BAD_PROTOCOL: return "the object does not expose a requested protocol";
        case eError::INSUFFICIENT_PERMISSIONS: return "insufficient permissions";
        case eError::DENIED: return "denied";
        case eError::UNKNOWN: break;
    }

    return "unknown error";
}
