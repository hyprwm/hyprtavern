#include "EventLoop.hpp"

#include <algorithm>
#include <cerrno>

#include <poll.h>

void CEventLoop::addFd(int fd, std::function<void()>&& onReady) {
    m_sources.emplace_back(SSource{.fd = fd, .onReady = std::move(onReady)});
}

void CEventLoop::removeFd(int fd) {
    std::erase_if(m_sources, [fd](const auto& source) { return source.fd == fd; });
}

void CEventLoop::setDeadline(Clock::time_point deadline, std::function<void()>&& onDeadline) {
    m_deadline = SDeadline{.when = deadline, .onDeadline = std::move(onDeadline)};
}

void CEventLoop::clearDeadline() {
    m_deadline.reset();
}

void CEventLoop::exit() {
    m_exit = true;
}

int CEventLoop::pollTimeout() const {
    if (!m_deadline)
        return -1;

    const auto REMAINING = std::chrono::ceil<std::chrono::milliseconds>(m_deadline->when - Clock::now());
    return std::max<int>(0, REMAINING.count());
}

bool CEventLoop::run() {
    m_exit = false;

    while (!m_exit) {
        std::vector<pollfd> fds;
        fds.reserve(m_sources.size());

        for (const auto& source : m_sources) {
            fds.emplace_back(pollfd{.fd = source.fd, .events = POLLIN, .revents = 0});
        }

        if (poll(fds.data(), fds.size(), pollTimeout()) < 0) {
            if (errno == EINTR)
                continue;

            return false;
        }

        if (m_deadline && Clock::now() >= m_deadline->when) {
            auto onDeadline = std::move(m_deadline->onDeadline);
            m_deadline.reset();
            onDeadline();
        }

        for (const auto& pfd : fds) {
            if (m_exit)
                break;

            if (!(pfd.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)))
                continue;

            // an earlier handler may have removed this source
            const auto SOURCE = std::ranges::find(m_sources, pfd.fd, &SSource::fd);
            if (SOURCE == m_sources.end())
                continue;

            // the handler may remove its own source
            auto onReady = SOURCE->onReady;
            onReady();
        }
    }

    return true;
}
