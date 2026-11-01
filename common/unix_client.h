#pragma once

#include "common/timer.h"

#include <functional>
#include <string>

namespace sensornode {

class EventLoop;

class UnixClient {
public:
    using LineHandler = std::function<void(const std::string& line)>;

    UnixClient(EventLoop& loop, std::string path);
    ~UnixClient();

    UnixClient(const UnixClient&) = delete;
    UnixClient& operator=(const UnixClient&) = delete;

    void on_line(LineHandler h) { on_line_ = std::move(h); }
    void start();
    void stop();

private:
    void try_connect();
    void on_readable();
    void drop();

    EventLoop&  loop_;
    std::string path_;
    int         fd_ = -1;
    std::string in_;
    bool        warned_ = false;
    Timer       retry_timer_;
    LineHandler on_line_;
};

} // namespace sensornode
