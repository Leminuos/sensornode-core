#pragma once

#include "otad/installer.h"
#include "otad/ota_types.h"
#include "otad/rebooter.h"
#include "otad/system_info.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace sensornode::ota {

class OtaManager {
public:
    using ChangeHandler = std::function<void(OtaChange, const Snapshot&)>;

    OtaManager(std::unique_ptr<Installer> installer, std::unique_ptr<Rebooter> rebooter,
               const SystemInfo& sys, const SystemStatus& status);

    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

    // url rỗng thì cài bản available. Error::none nghĩa là đã nhận lệnh, kết quả báo qua on_change.
    Error install(const std::string& url) { return dispatch({EventType::install, 0, url}); }

    // Chỉ nhận ở done.
    Error reboot() { return dispatch({EventType::reboot}); }

    // Manifest mới nhận, nullopt khi manifest bị xoá; chỉ giữ nếu mới hơn bản đang chạy.
    void offer(std::optional<Manifest> m);

    void set_force_update(bool force) noexcept { force_update_ = force; }

    const Snapshot& snapshot() const noexcept { return snap_; }

    void on_change(ChangeHandler h) { on_change_ = std::move(h); }

private:
    Error dispatch(const Event& e);

    // Mỗi state một handler, xem bảng ở đầu ota_manager.cpp.
    Error on_idle(const Event& e);
    Error on_downloading(const Event& e);
    Error on_installing(const Event& e);
    Error on_done(const Event& e);
    Error on_failed(const Event& e);
    Error on_rebooting(const Event& e);

    Error begin_install(const std::string& url);
    void  begin_reboot();
    void  finish_install();
    void  fail_install(const std::string& error);
    void  fail_reboot(const std::string& error);

    void enter(OtaState s);
    void update_percent(int percent);
    void set_available(std::optional<Manifest> m);
    void notify(OtaChange c);

    std::unique_ptr<Installer> installer_;
    std::unique_ptr<Rebooter>  rebooter_;
    const SystemInfo&          sys_;
    std::optional<std::string> running_version_;
    bool                       force_update_ = false;
    Snapshot                   snap_;
    ChangeHandler              on_change_;
};

} // namespace sensornode::ota
