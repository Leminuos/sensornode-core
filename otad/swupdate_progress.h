#pragma once

#include "common/timer.h"
#include "otad/installer.h"

#include <functional>
#include <string>

namespace sensornode::ota {

class ProgressClient {
public:
    using Handler = std::function<void(Phase phase, int percent)>;

    ProgressClient(EventLoop& loop, std::string path);
    ~ProgressClient();

    ProgressClient(const ProgressClient&) = delete;
    ProgressClient& operator=(const ProgressClient&) = delete;

    void start();
    void stop();

    void on_progress(Handler h) { on_progress_ = std::move(h); }

private:
    void try_connect();
    void on_readable();
    bool consume_ack();
    void dispatch(const char* raw);
    void disconnect();

    EventLoop&  loop_;
    std::string path_;
    Timer       retry_;
    int         fd_         = -1;
    std::string in_;
    bool        acked_      = false;
    bool        installing_ = false;
    unsigned    last_step_  = 0;
    Handler     on_progress_;
};

} // namespace sensornode::ota
