#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <vector>

// a minimal poll() loop over file descriptors, with a single optional deadline
class CEventLoop {
  public:
    using Clock = std::chrono::steady_clock;

    // onReady is called when the fd is readable or hung up
    void addFd(int fd, std::function<void()>&& onReady);
    void removeFd(int fd);

    // onDeadline is called once, unless the deadline is replaced or cleared first
    void setDeadline(Clock::time_point deadline, std::function<void()>&& onDeadline);
    void clearDeadline();

    // runs until exit(). Returns false if polling failed.
    bool run();
    void exit();

  private:
    struct SSource {
        int                   fd = -1;
        std::function<void()> onReady;
    };

    struct SDeadline {
        Clock::time_point     when;
        std::function<void()> onDeadline;
    };

    int                      pollTimeout() const;

    std::vector<SSource>     m_sources;
    std::optional<SDeadline> m_deadline;
    bool                     m_exit = false;
};
