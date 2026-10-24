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
        if (st.success()) return;
        on_event_({EventType::reboot_failed, 0, last_line_.empty() ? st.describe() : last_line_});
    });
}

bool Rebooter::start(std::string& err) {
    last_line_.clear();
    try {
        proc_.start(argv_);
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    SN_LOG_NOTICE("reboot", "spawned pid=%d cmd=%s", static_cast<int>(proc_.pid()), argv_[0].c_str());
    return true;
}

} // namespace sensornode::ota
