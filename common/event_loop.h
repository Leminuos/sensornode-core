#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace sensornode {

class EventLoop {
public:
    using Callback = std::function<void(std::uint32_t revents)>;

    EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void add(int fd, std::uint32_t events, Callback cb);
    void modify(int fd, std::uint32_t events);
    void remove(int fd);
    void run();
    void stop() noexcept { running_ = false; }

private:
    int epfd_;
    bool running_ = false;
    std::unordered_map<int, Callback> entries_;
};

} // namespace sensornode
