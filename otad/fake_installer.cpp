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
    explicit FakeInstaller(EventLoop& loop)
        : timer_(loop, [this](std::uint64_t) { step(); }) {}

    void start(const std::string& url) override {
        fail_    = url.find("fail") != std::string::npos;
        phase_   = Phase::downloading;
        percent_ = 0;
        report();
        timer_.start_periodic(kStepPeriod);
    }

private:
    void step() {
        percent_ += kStepPercent;

        if (phase_ == Phase::installing && fail_ && percent_ >= kFailAt) {
            finish(false, "simulated failure");
            return;
        }

        if (percent_ > 100) {
            if (phase_ == Phase::installing) {
                finish(true, {});
                return;
            }
            phase_   = Phase::installing;
            percent_ = 0;
        }
        report();
    }

    void report() {
        if (on_progress_) on_progress_(phase_, percent_);
    }

    void finish(bool ok, const std::string& error) {
        timer_.stop();
        if (on_finish_) on_finish_(ok, error);
    }

    Timer timer_;
    Phase phase_   = Phase::downloading;
    int   percent_ = 0;
    bool  fail_    = false;
};

} // namespace

std::unique_ptr<Installer> make_fake_installer(EventLoop& loop) {
    return std::make_unique<FakeInstaller>(loop);
}

} // namespace sensornode::ota
