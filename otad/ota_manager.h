#pragma once

#include "otad/installer.h"
#include "otad/manifest.h"
#include "otad/rebooter.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace sensornode::ota {

enum class OtaState { idle, downloading, installing, done, failed, rebooting };

const char* to_string(OtaState s);

class OtaManager {
public:
    struct Snapshot {
        OtaState    state   = OtaState::idle;
        int         percent = 0;
        std::string url;
        std::string error;
        std::optional<Manifest> available;  // bản mới hơn firmware đang chạy
    };

    using ChangeHandler = std::function<void(const Snapshot&)>;

    OtaManager(std::unique_ptr<Installer> installer, std::unique_ptr<Rebooter> rebooter,
               bool reboot_pending, std::optional<std::string> running_version);

    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

    // Trả về nullptr nếu đã bắt đầu cài, ngược lại là lý do từ chối. url rỗng thì cài bản available.
    const char* install(const std::string& url);

    // Chỉ nhận ở done. Trả về nullptr nếu đã chạy lệnh reboot, ngược lại là lý do từ chối.
    const char* reboot();

    // Manifest mới nhận, nullopt khi manifest bị xoá; chỉ giữ nếu mới hơn bản đang chạy.
    void offer(std::optional<Manifest> m);

    const Snapshot& snapshot() const noexcept { return snap_; }

    void on_state(ChangeHandler h)    { on_state_ = std::move(h); }
    void on_progress(ChangeHandler h) { on_progress_ = std::move(h); }
    void on_available(ChangeHandler h) { on_available_ = std::move(h); }

private:
    void set_state(OtaState s);
    void set_available(std::optional<Manifest> m);
    void handle_progress(Phase phase, int percent);
    void handle_finish(bool ok, const std::string& error);
    void handle_reboot_fail(const std::string& error);

    std::unique_ptr<Installer> installer_;
    std::unique_ptr<Rebooter>  rebooter_;
    std::optional<std::string> running_version_;
    Snapshot                   snap_;
    ChangeHandler              on_state_;
    ChangeHandler              on_progress_;
    ChangeHandler              on_available_;
};

} // namespace sensornode::ota
