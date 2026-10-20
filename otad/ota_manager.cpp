/*
 * Máy trạng thái OTA
 * ==================
 *
 *   +------+  install   +-------------+  phase=installing  +------------+  finish ok  +------+
 *   | idle | ---------> | downloading | -----------------> | installing | ----------> | done |
 *   +------+            +-------------+                    +------------+             +------+
 *                          ^     |                               |
 *                          |     | finish fail                   | finish fail
 *                  install |     v                               |
 *                          | +--------+                          |
 *                          +-| failed | <------------------------+
 *                            +--------+
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
 *   +-------------+------------------------------------------------+
 *
 *   Không có đường quay về idle, chỉ reboot mới thoát khỏi done.
 */

#include "otad/ota_manager.h"

#include "common/log.h"

#include <algorithm>

namespace sensornode::ota {

namespace {

constexpr std::size_t kMaxUrlLen = 1024;

// 09-swupdate-args ghép url vào tham số swupdate, khoảng trắng sẽ tách thành tham số mới.
bool valid_url(const std::string& url) {
    if (url.empty() || url.size() > kMaxUrlLen) return false;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
    return std::none_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f; });
}

} // namespace

const char* to_string(OtaState s) {
    switch (s) {
    case OtaState::idle:        return "idle";
    case OtaState::downloading: return "downloading";
    case OtaState::installing:  return "installing";
    case OtaState::done:        return "done";
    case OtaState::failed:      return "failed";
    }
    return "unknown";
}

OtaManager::OtaManager(std::unique_ptr<Installer> installer, bool reboot_pending)
    : installer_(std::move(installer)) {
    if (reboot_pending) {
        snap_.state   = OtaState::done;
        snap_.percent = 100;
    }

    if (installer_) {
        installer_->on_progress([this](Phase p, int pct) { handle_progress(p, pct); });
        installer_->on_finish([this](bool ok, const std::string& err) { handle_finish(ok, err); });
    }
}

const char* OtaManager::install(const std::string& url) {
    switch (snap_.state) {
    case OtaState::downloading:
    case OtaState::installing:  return "busy";
    case OtaState::done:        return "reboot pending";
    case OtaState::idle:
    case OtaState::failed:      break;
    }

    if (!installer_) return "installer not available";
    if (!valid_url(url)) return "invalid url";

    snap_.url     = url;
    snap_.error.clear();
    snap_.percent = 0;
    SN_LOG_NOTICE("ota", "install started url=%s", log::quote(url).c_str());
    set_state(OtaState::downloading);

    installer_->start(url);
    return nullptr;
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
        set_state(OtaState::done);
    } else {
        snap_.error = error.empty() ? "unknown error" : error;
        SN_LOG_ERR("ota", "install failed: %s url=%s", snap_.error.c_str(), log::quote(snap_.url).c_str());
        set_state(OtaState::failed);
    }
}

} // namespace sensornode::ota
