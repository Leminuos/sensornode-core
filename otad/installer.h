#pragma once

#include <functional>
#include <memory>
#include <string>

namespace sensornode {
class EventLoop;
}

namespace sensornode::ota {

enum class Phase { downloading, installing };

class Installer {
public:
    using ProgressHandler = std::function<void(Phase phase, int percent)>;
    using FinishHandler   = std::function<void(bool ok, const std::string& error)>;

    virtual ~Installer() = default;

    // Chỉ được gọi khi lần cài trước đã finish.
    virtual void start(const std::string& url) = 0;

    void on_progress(ProgressHandler h) { on_progress_ = std::move(h); }
    void on_finish(FinishHandler h)     { on_finish_ = std::move(h); }

protected:
    ProgressHandler on_progress_;
    FinishHandler   on_finish_;
};

// Giả lập tải + cài bằng timer, url chứa "fail" thì lỗi giữa lúc cài.
std::unique_ptr<Installer> make_fake_installer(EventLoop& loop);

// Chạy `tool <url>` (09-swupdate-args)
std::unique_ptr<Installer> make_swupdate_installer(EventLoop& loop, std::string tool);

} // namespace sensornode::ota
