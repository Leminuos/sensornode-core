#pragma once

#include "common/timer.h"
#include "otad/ota_types.h"

#include <string>

struct progress_msg;

namespace sensornode {
class EventLoop;
}

namespace sensornode::ota {

class ProgressClient {
public:
    ProgressClient(EventLoop& loop, std::string path, const EventHandler& handler);
    ~ProgressClient();

    ProgressClient(const ProgressClient&) = delete;
    ProgressClient& operator=(const ProgressClient&) = delete;

    void start();
    void stop();

private:
    void try_connect();
    void on_readable();
    void dispatch(const progress_msg& m);
    void disconnect();

    EventLoop&          loop_;
    std::string         path_;
    const EventHandler& handler_;
    Timer               retry_;
    int                 fd_        = -1;
    unsigned            last_step_ = 0;
};

} // namespace sensornode::ota
