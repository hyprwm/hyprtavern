#include "Core.hpp"
#include "../protocols/PermissionProtocol.hpp"
#include "../ui/Prompt.hpp"

#include <common/Log.hpp>

#include <algorithm>

constexpr std::string_view BARMAID_PROTOCOL                   = "hp_hyprtavern_barmaid_v1";
constexpr std::string_view PERMISSION_AUTHENTICATION_PROTOCOL = "hp_hyprtavern_permission_authentication_v1";

namespace {
    hpHyprtavernPermissionAuthenticationV1ResponseType toResponse(UI::eChoice choice) {
        switch (choice) {
            case UI::eChoice::ALLOW_SESSION: return HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_ONCE;
            case UI::eChoice::ALLOW_ALWAYS: return HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_ACCEPTED_PERSISTENT;
            default: return HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_DENIED;
        }
    }
}

CCore::~CCore() {
    // tears down all transactions, which still need the core
    m_server.reset();

    PROTO::permissions.reset();
    m_barmaid.reset();

    closeWindow();

    if (m_backend)
        m_backend->destroy();
}

bool CCore::init(int controlFd) {
    m_server = Hyprwire::IServerSocket::open();
    if (!m_server)
        return false;

    m_barmaid          = makeUnique<CBarmaidServer>(m_server);
    PROTO::permissions = makeUnique<CPermissionProtocol>(m_server);

    const auto CONTROL = m_server->addClient(controlFd);
    if (!CONTROL)
        return false;

    // the authenticated tavern is our only client
    CONTROL->setProtocolFilter([](std::string_view name) { return name == BARMAID_PROTOCOL || name == PERMISSION_AUTHENTICATION_PROTOCOL; });

    CONTROL->setDisconnectHandler([this] {
        g_logger->log(LOG_DEBUG, "the tavern closed the control channel, exiting");

        m_exit = true;
        m_loop.exit();

        if (m_showing)
            m_backend->destroy();
    });

    m_environmentListener = m_barmaid->m_events.environmentChanged.listen([this] { probeAvailability(); });

    m_loop.addFd(m_server->extractLoopFD(), [this] {
        dispatch();
        showPrompts();
    });

    probeAvailability();
    m_barmaid->setReady();

    return true;
}

bool CCore::run() {
    return m_loop.run();
}

void CCore::dispatch() {
    m_server->dispatchEvents(false);
}

SP<Hyprtoolkit::IBackend> CCore::createBackend() {
    Hyprtoolkit::IBackend::SBackendCreationData data;
    data.pLogConnection = makeShared<Hyprutils::CLI::CLoggerConnection>(*g_logger);

    return Hyprtoolkit::IBackend::createWithData(data);
}

void CCore::probeAvailability() {
    // only one backend may exist at a time
    if (m_showing) {
        m_probePending = true;
        return;
    }

    const auto BACKEND = createBackend();

    PROTO::permissions->setAvailable(!!BACKEND);

    if (BACKEND)
        BACKEND->destroy();
}

void CCore::enqueue(WP<CTransactionResource> transaction) {
    m_queue.emplace_back(std::move(transaction));
}

void CCore::dismiss(CTransactionResource* transaction) {
    std::erase_if(m_queue, [transaction](const auto& queued) { return !queued || queued.get() == transaction; });

    if (transaction != m_current)
        return;

    m_current = nullptr;
    closeWindow();

    if (m_showing)
        m_backend->addIdle([this] { showNext(); });
}

SP<CTransactionResource> CCore::nextTransaction() {
    while (!m_queue.empty()) {
        const auto NEXT = m_queue.front().lock();
        m_queue.pop_front();

        if (NEXT)
            return NEXT;
    }

    return nullptr;
}

void CCore::showPrompts() {
    std::erase_if(m_queue, [](const auto& queued) { return !queued; });

    if (m_showing || m_exit || m_queue.empty())
        return;

    if (PROTO::permissions->available())
        m_backend = createBackend();

    if (!m_backend) {
        PROTO::permissions->setAvailable(false);
        failPending();
        return;
    }

    // keep serving the tavern while prompts are shown, so they can be dismissed
    m_showing = true;
    m_backend->addFd(m_server->extractLoopFD(), [this] {
        dispatch();
        showNext();
    });

    showNext();
    m_backend->enterLoop();

    // left once nothing is queued anymore, or early if the display went away
    closeWindow();
    m_backend.reset();
    m_showing = false;

    failPending();

    if (m_probePending) {
        m_probePending = false;
        probeAvailability();
    }
}

void CCore::showNext() {
    if (!m_showing || m_window || m_exit)
        return;

    const auto NEXT = nextTransaction();
    if (!NEXT) {
        m_backend->destroy();
        return;
    }

    m_current = NEXT.get();
    m_window  = UI::openPrompt(m_backend, NEXT->request(), [this](UI::eChoice choice) { answer(toResponse(choice)); });
}

void CCore::answer(hpHyprtavernPermissionAuthenticationV1ResponseType response) {
    if (m_current)
        m_current->sendResult(response);

    m_current = nullptr;

    // not from within the window's own callbacks
    m_backend->addIdle([this] {
        closeWindow();
        showNext();
    });
}

void CCore::closeWindow() {
    if (!m_window)
        return;

    m_window->close();
    m_window.reset();
}

void CCore::failPending() {
    if (m_current)
        m_current->sendResult(HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_UNAVAILABLE);

    m_current = nullptr;

    while (const auto NEXT = nextTransaction()) {
        NEXT->sendResult(HP_HYPRTAVERN_PERMISSION_AUTHENTICATION_V1_RESPONSE_TYPE_UNAVAILABLE);
    }
}
