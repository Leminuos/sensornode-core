#pragma once

#include "common/process.h"
#include "otad/ota_types.h"

#include <string>
#include <vector>

namespace sensornode {
class EventLoop;
}

namespace sensornode::ota {

class Rebooter {
public:
    Rebooter(EventLoop& loop, std::vector<std::string> argv);

    Rebooter(const Rebooter&) = delete;
    Rebooter& operator=(const Rebooter&) = delete;

    bool start(std::string& err);

    void on_event(EventHandler h) { on_event_ = std::move(h); }

private:
    Process                  proc_;
    std::vector<std::string> argv_;
    std::string              last_line_;
    EventHandler             on_event_;
};

} // namespace sensornode::ota
