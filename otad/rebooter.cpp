#include "otad/rebooter.h"

#include "common/log.h"

#include <exception>

namespace sensornode::ota {

Rebooter::Rebooter(EventLoop& loop, std::vector<std::string> argv)
    : proc_(loop), argv_(std::move(argv)) {
    proc_.on_output([this](const std::string& line) {
        SN_LOG_INFO("reboot", "%s", line.c_str());
        last_line_ = line;
    });

    proc_.on_exit([this](const Process::ExitStatus& st) {
        SN_LOG_INFO("reboot", "command finished: %s", st.describe().c_str());
        if (st.success() || !on_fail_) return;
        on_fail_(last_line_.empty() ? st.describe() : last_line_);
    });
}

void Rebooter::start() {
    if (proc_.running()) return;
    last_line_.clear();
    try {
        proc_.start(argv_);
    } catch (const std::exception& e) {
        if (on_fail_) on_fail_(e.what());
        return;
    }
    SN_LOG_NOTICE("reboot", "spawned pid=%d cmd=%s", static_cast<int>(proc_.pid()), argv_[0].c_str());
}

} // namespace sensornode::ota
