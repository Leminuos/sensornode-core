#pragma once

#include "otad/installer.h"

#include <functional>
#include <memory>
#include <string>

namespace sensornode::ota {

enum class OtaState { idle, downloading, installing, done, failed };

const char* to_string(OtaState s);

class OtaManager {
public:
    struct Snapshot {
        OtaState    state   = OtaState::idle;
        int         percent = 0;
        std::string url;
        std::string error;
    };

    using ChangeHandler = std::function<void(const Snapshot&)>;

    // installer null nghĩa là chưa có backend, lệnh install bị từ chối.
    OtaManager(std::unique_ptr<Installer> installer, bool reboot_pending);

    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

    // Trả về nullptr nếu đã bắt đầu cài, ngược lại là lý do từ chối.
    const char* install(const std::string& url);

    const Snapshot& snapshot() const noexcept { return snap_; }

    void on_state(ChangeHandler h)    { on_state_ = std::move(h); }
    void on_progress(ChangeHandler h) { on_progress_ = std::move(h); }

private:
    void set_state(OtaState s);
    void handle_progress(Phase phase, int percent);
    void handle_finish(bool ok, const std::string& error);

    std::unique_ptr<Installer> installer_;
    Snapshot                   snap_;
    ChangeHandler              on_state_;
    ChangeHandler              on_progress_;
};

} // namespace sensornode::ota
