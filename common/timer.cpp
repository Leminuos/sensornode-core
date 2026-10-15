#include "common/timer.h"
#include "common/event_loop.h"
 
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
 
#include <cerrno>
#include <stdexcept>
#include <system_error>
 
namespace sensornode {

namespace {
    [[noreturn]] void throw_errno(const char* what) {
        throw std::system_error(errno, std::generic_category(), what);
    }

    timespec to_timespec(std::chrono::nanoseconds d) {
        if (d.count() < 0) {
            throw std::invalid_argument("Timer: negative duration");
        }
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(d);
        const auto nsec = d - secs;
    
        timespec ts{};
        ts.tv_sec  = static_cast<time_t>(secs.count());
        ts.tv_nsec = static_cast<long>(nsec.count());
        return ts;
    }
} // namespace

Timer::Timer(EventLoop& loop, Callback cb)
    : loop_(loop), cb_(std::move(cb)) {
    fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd_ < 0) throw_errno("timerfd_create");

    try {
        loop_.add(fd_, EPOLLIN, [this](std::uint32_t) { on_readable(); });
    } catch (...) {
        close(fd_);
        throw;
    }
}

Timer::~Timer() {
    loop_.remove(fd_);
    close(fd_);
}

void Timer::start_periodic(std::chrono::nanoseconds period) {
    if (period.count() <= 0) {
        throw std::invalid_argument("Timer::start_periodic: period must be > 0");
    }

    arm(period, period);
}

void Timer::start_oneshot(std::chrono::nanoseconds delay) {
    if (delay.count() == 0) delay = std::chrono::nanoseconds{1};
    arm(delay, std::chrono::nanoseconds{0});
}

void Timer::stop() {
    arm(std::chrono::nanoseconds{0}, std::chrono::nanoseconds{0});
}

void Timer::arm(std::chrono::nanoseconds first, std::chrono::nanoseconds interval) {
    itimerspec spec{};
    spec.it_value    = to_timespec(first);
    spec.it_interval = to_timespec(interval);

    if (timerfd_settime(fd_, 0, &spec, nullptr) < 0) {
        throw_errno("timerfd_settime");
    }
}

void Timer::on_readable() {
    std::uint64_t expirations = 0;

    for (;;) {

        const ssize_t r = read(fd_, &expirations, sizeof expirations);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) return;
            throw_errno("read(timerfd)");
        }

        if (r != static_cast<ssize_t>(sizeof expirations)) {
            throw std::runtime_error("read(timerfd): short read");
        }

        break;
    }

    cb_(expirations);
}

std::chrono::nanoseconds monotonic_now() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    

    return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

} // namespace sensornode
