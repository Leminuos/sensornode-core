#include "common/event_loop.h"
#include "common/log.h"

#include <sys/epoll.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sensornode {

namespace {
constexpr int kMaxEvents = 16;

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}
} // namespace

EventLoop::EventLoop() : epfd_(epoll_create1(EPOLL_CLOEXEC)) {

    if (epfd_ < 0) {
        throw_errno("epoll_create1");
    }
}

void EventLoop::add(int fd, std::uint32_t events, Callback cb) {
    if (entries_.count(fd) != 0) {
        throw std::logic_error("EventLoop::add: fd already registered");
    }

    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw_errno("epoll_ctl(ADD)");
    }

    entries_.emplace(fd, std::move(cb));
}

void EventLoop::modify(int fd, std::uint32_t events) {
    const auto it = entries_.find(fd);
    if (it == entries_.end()) {
        throw std::logic_error("EventLoop::modify: fd not registered");
    }

    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        throw_errno("epoll_ctl(MOD)");
    }
}

void EventLoop::remove(int fd) {
    const auto it = entries_.find(fd);
    if (it == entries_.end()) return;

    // EBADF: fd đã bị close (vd libmosquitto tự close), kernel tự gỡ khỏi epoll.
    if (epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) < 0 && errno != EBADF) {
        SN_LOG_ERR("loop", "epoll_ctl(DEL) failed: %s fd=%d", log::errstr(errno).c_str(), fd);
    }

    entries_.erase(it);
}

void EventLoop::run() {
    running_ = true;
    struct epoll_event events[kMaxEvents];

    while (running_) {
        const int n = epoll_wait(epfd_, events, kMaxEvents, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw_errno("epoll_wait");
        }

        for (int i = 0; i < n; ++i) {
            const std::uint32_t revents = events[i].events;
            const int fd = events[i].data.fd;

            const auto it = entries_.find(fd);
            if (it == entries_.end()) continue;

            const Callback cb = it->second;
            cb(revents);
        }
    }
}

} // namespace sensornode
