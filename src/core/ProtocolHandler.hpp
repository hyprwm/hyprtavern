#pragma once

#include <hp_hyprtavern_core_v1-server.hpp>

#include "../helpers/Memory.hpp"

struct SQueryData {
    std::vector<std::string>             protocolNames;
    hpHyprtavernCoreV1BusQueryFilterMode protoFilter = HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL;
    std::vector<std::string>             props;
    hpHyprtavernCoreV1BusQueryFilterMode propFilter = HP_HYPRTAVERN_CORE_V1_BUS_QUERY_FILTER_MODE_ALL;
};

class CBusQuery {
  public:
    CBusQuery(SP<CHpHyprtavernBusQueryV1Object>&& obj, SQueryData&& data);
    ~CBusQuery() = default;

    SQueryData m_data;

  private:
    SP<CHpHyprtavernBusQueryV1Object> m_object;
};

class CBusObject {
  public:
    CBusObject(SP<CHpHyprtavernBusObjectV1Object>&& obj, const char* name);
    ~CBusObject() = default;

    void                                             sendNewConnection(int fd);

    std::vector<std::pair<std::string, uint32_t>>    m_protocols;
    std::vector<std::pair<std::string, std::string>> m_props;

    std::string                                      m_name;

    size_t                                           m_internalID = 0;

  private:
    SP<CHpHyprtavernBusObjectV1Object> m_object;
};

class CBusObjectHandle {
  public:
    CBusObjectHandle(SP<CHpHyprtavernBusObjectHandleV1Object>&& obj, SP<CBusObject> busObject);
    ~CBusObjectHandle() = default;

    WP<CBusObject> m_busObject;

  private:
    SP<CHpHyprtavernBusObjectHandleV1Object> m_object;
};

class CCoreManagerObject {
  public:
    CCoreManagerObject(SP<CHpHyprtavernCoreManagerV1Object>&& obj);
    ~CCoreManagerObject() = default;

  private:
    SP<CHpHyprtavernCoreManagerV1Object> m_object;
};

class CCoreProtocolHandler {
  public:
    CCoreProtocolHandler()  = default;
    ~CCoreProtocolHandler() = default;

    void init(SP<Hyprwire::IServerSocket> sock);

    //
    void removeObject(CCoreManagerObject* obj);
    void removeObject(CBusObject* obj);
    void removeObject(CBusObjectHandle* obj);
    void removeObject(CBusQuery* obj);

    //
    std::vector<SP<CCoreManagerObject>> m_managers;
    std::vector<SP<CBusObject>>         m_objects;
    std::vector<SP<CBusObjectHandle>>   m_handles;
    std::vector<SP<CBusQuery>>          m_queries;

    SP<CBusObject>                      fromID(uint32_t id);

    WP<Hyprwire::IServerSocket>         m_sock;
};

inline UP<CCoreProtocolHandler> g_coreProto;
