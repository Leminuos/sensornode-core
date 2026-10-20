#include "otad/installer.h"

#include "common/log.h"
#include "common/process.h"
#include "otad/swupdate_progress.h"

#include <exception>

namespace sensornode::ota {

namespace {

class SwupdateInstaller final : public Installer {
public:
    SwupdateInstaller(EventLoop& loop, std::string tool, std::string progress_socket)
        : proc_(loop), progress_(loop, std::move(progress_socket), on_event_), tool_(std::move(tool)) {
        proc_.on_output([this](const std::string& line) { handle_output(line); });
        proc_.on_exit([this](const Process::ExitStatus& st) { handle_exit(st); });
    }

    bool start(const std::string& url, std::string& err) override {
        last_error_.clear();
        try {
            proc_.start({tool_, url});
        } catch (const std::exception& e) {
            err = e.what();
            return false;
        }
        SN_LOG_INFO("swupdate", "spawned pid=%d tool=%s", static_cast<int>(proc_.pid()), tool_.c_str());
        progress_.start();
        return true;
    }

private:
    void handle_output(const std::string& line) {
        SN_LOG_INFO("swupdate", "%s", line.c_str());
        if (line.find("ERROR") != std::string::npos) last_error_ = line;
    }

    void handle_exit(const Process::ExitStatus& st) {
        progress_.stop();
        SN_LOG_INFO("swupdate", "finished: %s", st.describe().c_str());
        if (st.success()) {
            on_event_({EventType::install_ok});
        } else {
            on_event_({EventType::install_failed, 0, last_error_.empty() ? st.describe() : last_error_});
        }
    }

    Process        proc_;
    ProgressClient progress_;
    std::string    tool_;
    std::string    last_error_;
};

} // namespace

std::unique_ptr<Installer> make_swupdate_installer(EventLoop& loop, std::string tool, std::string progress_socket) {
    return std::make_unique<SwupdateInstaller>(loop, std::move(tool), std::move(progress_socket));
}

} // namespace sensornode::ota
