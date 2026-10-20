#pragma once

#include "common/timer.h"

#include <chrono>
#include <functional>
#include <string_view>

namespace sensornode {

class EventLoop;

namespace systemd {

// Gửi trạng thái cho service manager
bool notify(std::string_view state);

// Chu kỳ watchdog do systemd cấu hình
std::chrono::microseconds watchdog_timeout();

} // namespace systemd

class Watchdog {
public:
    using HealthCheck = std::function<bool()>;

    Watchdog(EventLoop& loop, HealthCheck healthy);

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    bool enabled() const noexcept { return timeout_.count() > 0; }
    std::chrono::microseconds timeout() const noexcept { return timeout_; }

private:
    void on_tick();

    HealthCheck               healthy_;
    std::chrono::microseconds timeout_;
    Timer                     timer_;
    bool                      was_healthy_ = true;
};

} // namespace sensornode
