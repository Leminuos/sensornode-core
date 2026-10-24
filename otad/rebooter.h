#pragma once

#include "common/process.h"

#include <functional>
#include <string>
#include <vector>

namespace sensornode {
class EventLoop;
}

namespace sensornode::ota {

// Chạy lệnh reboot (mặc định `systemctl reboot`). Thành công thì systemd gửi SIGTERM
// cho otad, nên chỉ cần báo lại khi lệnh lỗi.
class Rebooter {
public:
    using FailHandler = std::function<void(const std::string& error)>;

    Rebooter(EventLoop& loop, std::vector<std::string> argv);

    Rebooter(const Rebooter&) = delete;
    Rebooter& operator=(const Rebooter&) = delete;

    void start();
    void on_fail(FailHandler h) { on_fail_ = std::move(h); }

private:
    Process                  proc_;
    std::vector<std::string> argv_;
    std::string              last_line_;
    FailHandler              on_fail_;
};

} // namespace sensornode::ota
