#include "otad/installer.h"

#include "common/timer.h"

#include <chrono>
#include <cstdint>

namespace sensornode::ota {

namespace {

using namespace std::chrono_literals;

constexpr auto kStepPeriod  = 200ms;
constexpr int  kStepPercent = 10;
constexpr int  kFailAt      = 50;

class FakeInstaller final : public Installer {
public:
    explicit FakeInstaller(EventLoop& loop) : timer_(loop, [this](std::uint64_t) { step(); }) {}

    bool start(const std::string& url, std::string&) override {
        fail_     = url.find("fail") != std::string::npos;
        progress_ = EventType::download_progress;
        percent_  = 0;
        timer_.start_periodic(kStepPeriod);
        return true;
    }

private:
    void step() {
        percent_ += kStepPercent;

        if (progress_ == EventType::install_progress && fail_ && percent_ >= kFailAt) {
            finish({EventType::install_failed, 0, "simulated failure"});
            return;
        }

        if (percent_ > 100) {
            if (progress_ == EventType::install_progress) {
                finish({EventType::install_ok});
                return;
            }
            progress_ = EventType::install_progress;
            percent_  = 0;
        }
        on_event_({progress_, percent_});
    }

    void finish(const Event& e) {
        timer_.stop();
        on_event_(e);
    }

    Timer     timer_;
    EventType progress_ = EventType::download_progress;
    int       percent_  = 0;
    bool      fail_     = false;
};

} // namespace

std::unique_ptr<Installer> make_fake_installer(EventLoop& loop) {
    return std::make_unique<FakeInstaller>(loop);
}

} // namespace sensornode::ota
