/*
 * Máy trạng thái OTA
 * ==================
 *
 *   +------+  install   +-------------+  phase=installing  +------------+  finish ok  +------+
 *   | idle | ---------> | downloading | -----------------> | installing | ----------> | done |
 *   +------+            +-------------+                    +------------+             +------+
 *                          ^     |                               |                       |  ^
 *                          |     | finish fail                   | finish fail    reboot |  | lỗi
 *                  install |     v                               |                       v  |
 *                          | +--------+                          |                  +-----------+
 *                          +-| failed | <------------------------+                  | rebooting |
 *                            +--------+                                             +-----------+
 *
 *
 *   Khởi động khi running_slot != active_slot và ustate=1 thì vào thẳng done.
 *
 *   +-------------+------------------------------------------------+
 *   | Trạng thái  | Lệnh install                                   |
 *   +=============+================================================+
 *   | idle        | nhận                                           |
 *   | failed      | nhận, cài lại từ đầu                           |
 *   | downloading | từ chối "busy"                                 |
 *   | installing  | từ chối "busy"                                 |
 *   | done        | từ chối "reboot pending", slot kia đã được ghi |
 *   | rebooting   | từ chối "reboot pending"                       |
 *   +-------------+------------------------------------------------+
 */

#include "otad/ota_manager.h"

#include "common/log.h"

#include <algorithm>

namespace sensornode::ota {

const char* to_string(OtaState s) {
    switch (s) {
    case OtaState::idle:        return "idle";
    case OtaState::downloading: return "downloading";
    case OtaState::installing:  return "installing";
    case OtaState::done:        return "done";
    case OtaState::failed:      return "failed";
    case OtaState::rebooting:   return "rebooting";
    }
    return "unknown";
}

OtaManager::OtaManager(std::unique_ptr<Installer> installer, std::unique_ptr<Rebooter> rebooter,
                       bool reboot_pending, std::optional<std::string> running_version)
    : installer_(std::move(installer)),
      rebooter_(std::move(rebooter)),
      running_version_(std::move(running_version)) {
    if (reboot_pending) {
        snap_.state   = OtaState::done;
        snap_.percent = 100;
    }

    installer_->on_progress([this](Phase p, int pct) { handle_progress(p, pct); });
    installer_->on_finish([this](bool ok, const std::string& err) { handle_finish(ok, err); });
    rebooter_->on_fail([this](const std::string& err) { handle_reboot_fail(err); });
}

const char* OtaManager::install(const std::string& url) {
    switch (snap_.state) {
    case OtaState::downloading:
    case OtaState::installing:  return "busy";
    case OtaState::done:
    case OtaState::rebooting:   return "reboot pending";
    case OtaState::idle:
    case OtaState::failed:      break;
    }

    if (url.empty() && !snap_.available) return "no update available";
    const std::string target = url.empty() ? snap_.available->url : url;
    // 09-swupdate-args ghép url vào tham số swupdate, khoảng trắng sẽ tách thành tham số mới.
    if (!valid_url(target)) return "invalid url";

    snap_.url     = target;
    snap_.error.clear();
    snap_.percent = 0;
    SN_LOG_NOTICE("ota", "install started url=%s", log::quote(target).c_str());
    set_state(OtaState::downloading);

    installer_->start(target);
    return nullptr;
}

const char* OtaManager::reboot() {
    switch (snap_.state) {
    case OtaState::done:        break;
    case OtaState::rebooting:   return "already rebooting";
    case OtaState::downloading:
    case OtaState::installing:  return "busy";
    case OtaState::idle:
    case OtaState::failed:      return "no update installed";
    }

    snap_.error.clear();
    SN_LOG_NOTICE("ota", "reboot requested");
    set_state(OtaState::rebooting);
    rebooter_->start();
    return nullptr;
}

// Slot mới vẫn đã được ghi, quay về done để client thử reboot lại.
void OtaManager::handle_reboot_fail(const std::string& error) {
    if (snap_.state != OtaState::rebooting) return;
    snap_.error = "reboot failed: " + error;
    SN_LOG_ERR("ota", "%s", snap_.error.c_str());
    set_state(OtaState::done);
}

void OtaManager::offer(std::optional<Manifest> m) {
    if (m) {
        if (!running_version_) {
            SN_LOG_WARN("ota", "manifest ignored: running version unknown version=%s", m->version.c_str());
            m.reset();
        } else if (compare_versions(m->version, *running_version_) <= 0) {
            SN_LOG_INFO("ota", "manifest not newer version=%s running=%s",
                        m->version.c_str(), running_version_->c_str());
            m.reset();
        }
    }
    set_available(std::move(m));
}

void OtaManager::set_available(std::optional<Manifest> m) {
    if (m == snap_.available) return;
    snap_.available = std::move(m);
    if (snap_.available) {
        SN_LOG_NOTICE("ota", "update available version=%s url=%s", snap_.available->version.c_str(),
                      log::quote(snap_.available->url).c_str());
    } else {
        SN_LOG_INFO("ota", "no update available");
    }
    if (on_available_) on_available_(snap_);
}

void OtaManager::set_state(OtaState s) {
    snap_.state = s;
    SN_LOG_INFO("ota", "state=%s", to_string(s));
    if (on_state_) on_state_(snap_);
}

void OtaManager::handle_progress(Phase phase, int percent) {
    if (snap_.state != OtaState::downloading && snap_.state != OtaState::installing) return;

    percent = std::clamp(percent, 0, 100);

    if (phase == Phase::installing && snap_.state == OtaState::downloading) {
        snap_.percent = percent;
        set_state(OtaState::installing);
        return;
    }

    // SWUpdate gửi progress rất dày, chỉ báo client khi phần trăm đổi.
    if (percent == snap_.percent) return;
    snap_.percent = percent;
    if (on_progress_) on_progress_(snap_);
}

void OtaManager::handle_finish(bool ok, const std::string& error) {
    if (ok) {
        snap_.percent = 100;
        SN_LOG_NOTICE("ota", "install succeeded url=%s", log::quote(snap_.url).c_str());
        // Bản vừa cài không còn là "bản mới" để mời cài nữa.
        if (snap_.available && snap_.available->url == snap_.url) snap_.available.reset();
        set_state(OtaState::done);
    } else {
        snap_.error = error.empty() ? "unknown error" : error;
        SN_LOG_ERR("ota", "install failed: %s url=%s", snap_.error.c_str(), log::quote(snap_.url).c_str());
        set_state(OtaState::failed);
    }
}

} // namespace sensornode::ota
