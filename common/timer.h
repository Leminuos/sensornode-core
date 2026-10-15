#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace sensornode {

class EventLoop;

class Timer {
public:
    using Callback = std::function<void(std::uint64_t expirations)>;

    Timer(EventLoop& loop, Callback cb);
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    void start_periodic(std::chrono::nanoseconds period);
    void start_oneshot(std::chrono::nanoseconds delay);

    void stop();

private:
    void arm(std::chrono::nanoseconds first, std::chrono::nanoseconds interval);
    void on_readable();

    EventLoop& loop_;
    Callback cb_;
    int fd_ = -1;
};

std::chrono::nanoseconds monotonic_now();

} // namespace sensornode
