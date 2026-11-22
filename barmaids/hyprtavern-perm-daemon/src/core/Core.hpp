#pragma once

#include <common/EventLoop.hpp>
#include <common/Memory.hpp>
#include <shared/BarmaidServer.hpp>

#include <hp_hyprtavern_permission_authentication_v1-server.hpp>

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/window/Window.hpp>

#include <hyprutils/signal/Listener.hpp>
#include <hyprwire/hyprwire.hpp>

#include <deque>

class CTransactionResource;

class CCore {
  public:
    CCore() = default;
    ~CCore();

    bool init(int controlFd);
    bool run();

    // queues a frozen transaction for prompting
    void enqueue(WP<CTransactionResource> transaction);

    // dismisses the prompt of a destroyed transaction, if shown
    void dismiss(CTransactionResource* transaction);

  private:
    void dispatch();
    void probeAvailability();

    // runs the toolkit for as long as prompts are queued
    void                                   showPrompts();
    void                                   showNext();
    void                                   answer(hpHyprtavernPermissionAuthenticationV1ResponseType response);
    void                                   closeWindow();
    void                                   failPending();
    SP<CTransactionResource>               nextTransaction();

    SP<Hyprtoolkit::IBackend>              createBackend();

    SP<Hyprwire::IServerSocket>            m_server;
    UP<CBarmaidServer>                     m_barmaid;
    CEventLoop                             m_loop;
    Hyprutils::Signal::CHyprSignalListener m_environmentListener;

    std::deque<WP<CTransactionResource>>   m_queue;
    CTransactionResource*                  m_current = nullptr;

    SP<Hyprtoolkit::IBackend>              m_backend;
    SP<Hyprtoolkit::IWindow>               m_window;
    bool                                   m_showing      = false;
    bool                                   m_probePending = false;
    bool                                   m_exit         = false;
};

inline UP<CCore> g_core;
