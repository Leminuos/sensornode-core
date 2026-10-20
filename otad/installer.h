#pragma once

#include "otad/ota_types.h"

#include <memory>
#include <string>

namespace sensornode {
class EventLoop;
}

namespace sensornode::ota {

class Installer {
public:
    virtual ~Installer() = default;

    virtual bool start(const std::string& url, std::string& err) = 0;

    void on_event(EventHandler h) { on_event_ = std::move(h); }

protected:
    EventHandler on_event_;
};

// Giả lập tải + cài bằng timer, url chứa fail thì lỗi giữa lúc cài.
std::unique_ptr<Installer> make_fake_installer(EventLoop& loop);

// Chạy `tool <url>` (09-swupdate-args)
std::unique_ptr<Installer> make_swupdate_installer(EventLoop& loop, std::string tool);

} // namespace sensornode::ota
