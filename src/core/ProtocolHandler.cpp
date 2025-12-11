#include "ProtocolHandler.hpp"
#include "../helpers/Logger.hpp"

#include <algorithm>
#include <format>

#include <sys/socket.h>

static SP<CHpHyprtavernCoreV1Impl> coreImpl;
static uint32_t                    maxId = 1;

CBusQuery::CBusQuery(SP<CHpHyprtavernBusQueryV1Object>&& obj, SQueryData&& data) : m_data(std::move(data)), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    g_logger->log(LOG_DEBUG, "new query with {} protocols and {} props", m_data.protocolNames.size(), m_data.props.size());

    // run the query
    std::vector<uint32_t> matches;
    for (const auto& obj : g_coreProto->m_objects) {

        // protocols
        if (!m_data.protocolNames.empty()) {
            if (m_data.protoFilter == HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL) {
                bool matched = true;
                for (const auto& p : m_data.protocolNames) {
                    if (std::ranges::find_if(obj->m_protocols, [&p](const auto& e) { return e.first == p; }) != obj->m_protocols.end())
                        continue;

                    matched = false;
                    break;
                }

                if (!matched)
                    continue;
            } else {
                bool matched = false;
                for (const auto& p : m_data.protocolNames) {
                    if (std::ranges::find_if(obj->m_protocols, [&p](const auto& e) { return e.first == p; }) == obj->m_protocols.end())
                        continue;

                    matched = true;
                    break;
                }

                if (!matched)
                    continue;
            }
        }

        // properties
        if (!m_data.props.empty()) {
            if (m_data.propFilter == HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL) {
                bool matched = true;
                for (const auto& p : m_data.props) {
                    if (!p.contains('=')) {
                        m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property in query");
                        return;
                    }
                    size_t           eqPos    = p.find('=');
                    std::string_view propName = std::string_view(p).substr(0, eqPos);
                    std::string_view propVal  = std::string_view(p).substr(eqPos + 1);

                    if (std::ranges::find_if(obj->m_props, [&propName, &propVal](const auto& e) { return e.first == propName && e.second == propVal; }) != obj->m_props.end())
                        continue;

                    matched = false;
                    break;
                }

                if (!matched)
                    continue;
            } else {
                bool matched = false;
                for (const auto& p : m_data.props) {
                    if (!p.contains('=')) {
                        m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property in query");
                        return;
                    }
                    size_t           eqPos    = p.find('=');
                    std::string_view propName = std::string_view(p).substr(0, eqPos);
                    std::string_view propVal  = std::string_view(p).substr(eqPos + 1);

                    if (std::ranges::find_if(obj->m_props, [&propName, &propVal](const auto& e) { return e.first == propName && e.second == propVal; }) == obj->m_props.end())
                        continue;

                    matched = true;
                    break;
                }

                if (!matched)
                    continue;
            }
        }

        // matched
        matches.emplace_back(obj->m_internalID);
    }

    g_logger->log(LOG_DEBUG, "query got {} matches", matches.size());

    // send the matches
    m_object->sendResults(matches);
}

CBusObject::CBusObject(SP<CHpHyprtavernBusObjectV1Object>&& obj, const char* name) : m_name(name), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_internalID = maxId++;

    g_logger->log(LOG_DEBUG, "new bus object gets id {}", m_internalID);

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setExposeProtocol([this](const char* name, uint32_t rev) {
        m_protocols.emplace_back(std::make_pair<>(name, rev)); //
    });

    m_object->setExposeProperty([this](const char* n, const char* v) {
        std::string_view name  = n;
        std::string_view value = v;

        if (name.empty()) {
            m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property name (empty)");
            return;
        }

        if (!std::ranges::all_of(name,
                                 [](const char& c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '+' || c == ':' || (c >= '0' && c <= '9'); })) {
            m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property name (invalid chars)");
            return;
        }

        if (std::ranges::count(name, ':') != 1 || name.front() == ':' || name.back() == ':') {
            m_object->error(HP_HYPRTAVERN_CORE_V1_BUS_OBJECT_ERRORS_INVALID_PROPERTY_NAME, "Invalid property name (invalid colons)");
            return;
        }

        if (value.empty()) {
            std::erase_if(m_props, [&name](const auto& e) { return e.first == name; });
            return;
        }

        m_props.emplace_back(std::make_pair<>(name, value));
    });
}

void CBusObject::sendNewConnection(int fd) {
    m_object->sendNewFd(fd);
}

CBusObjectHandle::CBusObjectHandle(SP<CHpHyprtavernBusObjectHandleV1Object>&& obj, SP<CBusObject> busObject) : m_busObject(busObject), m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setConnect([this]() {
        if (!m_busObject) {
            m_object->sendSocketFailed();
            return;
        }

        int fds[2];

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            g_logger->log(LOG_ERR, "failed to create a socketpair");
            m_object->sendSocketFailed();
            return;
        }

        m_object->sendSocket(fds[0]);
        m_busObject->sendNewConnection(fds[1]);
    });

    // send the data about the object

    if (!m_busObject) {
        g_logger->log(LOG_DEBUG, "new object handle for invalid object");
        m_object->sendFailed();
        return;
    }

    g_logger->log(LOG_DEBUG, "new object handle for object id {}", m_busObject->m_internalID);

    m_object->sendName(m_busObject->m_name.c_str());

    {
        std::vector<const char*> names;
        std::vector<uint32_t>    revs;

        names.reserve(m_busObject->m_protocols.size());
        revs.reserve(m_busObject->m_protocols.size());

        for (const auto& [n, r] : m_busObject->m_protocols) {
            names.emplace_back(n.c_str());
            revs.emplace_back(r);
        }

        m_object->sendProtocols(names, revs);
    }

    {
        std::vector<std::string> container;
        std::vector<const char*> strs;

        container.reserve(m_busObject->m_props.size());
        strs.reserve(m_busObject->m_props.size());

        for (const auto& [n, v] : m_busObject->m_props) {
            container.emplace_back(std::format("{}={}", n, v));
            strs.emplace_back(container.back().c_str());
        }

        m_object->sendProperties(strs);
    }

    m_object->sendDone();
}

CCoreManagerObject::CCoreManagerObject(SP<CHpHyprtavernCoreManagerV1Object>&& obj) : m_object(std::move(obj)) {
    if (!m_object->getObject())
        return;

    m_object->setOnDestroy([this]() { g_coreProto->removeObject(this); });
    m_object->setDestroy([this]() { g_coreProto->removeObject(this); });

    m_object->setGetBusObject([this](uint32_t seq, const char* objectName) {
        g_coreProto->m_objects.emplace_back( //
            makeShared<CBusObject>(          //
                makeShared<CHpHyprtavernBusObjectV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_object_v1", seq)), //
                objectName                                                                                                                          //
                ));
    });

    m_object->setGetObjectHandle([this](uint32_t seq, uint32_t id) {
        g_coreProto->m_handles.emplace_back( //
            makeShared<CBusObjectHandle>(    //
                makeShared<CHpHyprtavernBusObjectHandleV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_object_handle_v1", seq)), //
                g_coreProto->fromID(id)                                                                                                                    //
                ));
    });

    m_object->setGetQueryObject([this](uint32_t seq, std::vector<const char*> protos, hpHyprtavernCoreV1BusQueryFilterMode protoMode, std::vector<const char*> props,
                                       hpHyprtavernCoreV1BusQueryFilterMode propMode) {
        SQueryData data;
        data.propFilter  = propMode;
        data.protoFilter = protoMode;

        data.props.reserve(props.size());
        data.protocolNames.reserve(protos.size());

        for (const auto& pn : protos) {
            data.protocolNames.emplace_back(pn);
        }

        for (const auto& pn : props) {
            data.props.emplace_back(pn);
        }

        g_coreProto->m_queries.emplace_back( //
            makeShared<CBusQuery>(           //
                makeShared<CHpHyprtavernBusQueryV1Object>(
                    g_coreProto->m_sock->createObject(m_object->getObject()->client(), m_object->getObject(), "hp_hyprtavern_bus_query_v1", seq)), //
                std::move(data)                                                                                                                    //
                ));
    });
}

void CCoreProtocolHandler::init(SP<Hyprwire::IServerSocket> sock) {
    coreImpl = makeShared<CHpHyprtavernCoreV1Impl>(1, [this](SP<Hyprwire::IObject> obj) {
        m_managers.emplace_back(makeShared<CCoreManagerObject>(makeShared<CHpHyprtavernCoreManagerV1Object>(std::move(obj)))); //
    });

    sock->addImplementation(coreImpl);

    m_sock = sock;
}

void CCoreProtocolHandler::removeObject(CCoreManagerObject* obj) {
    std::erase_if(m_managers, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CBusQuery* obj) {
    std::erase_if(m_queries, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CBusObject* obj) {
    std::erase_if(m_objects, [obj](const auto& e) { return e.get() == obj; });
}

void CCoreProtocolHandler::removeObject(CBusObjectHandle* obj) {
    std::erase_if(m_handles, [obj](const auto& e) { return e.get() == obj; });
}

SP<CBusObject> CCoreProtocolHandler::fromID(uint32_t id) {
    for (const auto& o : m_objects) {
        if (o->m_internalID != id)
            continue;

        return o;
    }

    return nullptr;
}
