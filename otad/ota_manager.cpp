/*
 * Máy trạng thái OTA
 * ==================
 *
 *   +------+  install   +-------------+  install_progress  +------------+  install_ok  +------+
 *   | idle | ---------> | downloading | -----------------> | installing | -----------> | done |
 *   +------+            +-------------+                    +------------+              +------+
 *                          ^     |                               |
 *                          |     | install_failed                | install_failed
 *                  install |     v                               |
 *                          | +--------+                          |
 *                          +-| failed | <------------------------+
 *                            +--------+
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

OtaManager::OtaManager(std::unique_ptr<Installer> installer, const SystemInfo& sys, const SystemStatus& status)
    : installer_(std::move(installer)),
      sys_(sys) {
    if (status.reboot_pending()) {
        snap_.state   = OtaState::done;
        snap_.percent = 100;
    }

    installer_->on_event([this](const Event& e) { dispatch(e); });
}

Error OtaManager::dispatch(const Event& e) {
    switch (snap_.state) {
    case OtaState::idle:        return on_idle(e);
    case OtaState::downloading: return on_downloading(e);
    case OtaState::installing:  return on_installing(e);
    case OtaState::done:        return on_done(e);
    case OtaState::failed:      return on_failed(e);
    }
    return Error::none;
}

Error OtaManager::on_idle(const Event& e) {
    switch (e.type) {
    case EventType::install: return begin_install(e.text);
    case EventType::download_progress:
    case EventType::install_progress:
    case EventType::install_ok:
    case EventType::install_failed:
        break;
    }
    return Error::none;
}

Error OtaManager::on_downloading(const Event& e) {
    switch (e.type) {
    case EventType::install:
        return Error::busy;
    case EventType::download_progress:
        update_percent(e.percent);
        break;
    case EventType::install_progress:
        snap_.percent = std::clamp(e.percent, 0, 100);
        enter(OtaState::installing);
        break;
    case EventType::install_ok:
        finish_install();
        break;
    case EventType::install_failed:
        fail_install(e.text);
        break;
    }
    return Error::none;
}

Error OtaManager::on_installing(const Event& e) {
    switch (e.type) {
    case EventType::install:
        return Error::busy;
    case EventType::install_progress:
        update_percent(e.percent);
        break;
    case EventType::install_ok:
        finish_install();
        break;
    case EventType::install_failed:
        fail_install(e.text);
        break;
    case EventType::download_progress:
        break;
    }
    return Error::none;
}

Error OtaManager::on_done(const Event& e) {
    switch (e.type) {
    case EventType::install:
        return Error::reboot_pending;
    case EventType::download_progress:
    case EventType::install_progress:
    case EventType::install_ok:
    case EventType::install_failed:
        break;
    }
    return Error::none;
}

Error OtaManager::on_failed(const Event& e) {
    return on_idle(e);
}

Error OtaManager::begin_install(const std::string& url) {
    if (!valid_url(url)) return Error::invalid_url;
    if (sys_.read().trial_boot()) return Error::trial_boot;

    snap_.url     = url;
    snap_.error.clear();
    snap_.percent = 0;
    SN_LOG_NOTICE("ota", "install started url=%s", log::quote(url).c_str());
    enter(OtaState::downloading);

    std::string error;
    if (!installer_->start(url, error)) fail_install(error);
    return Error::none;
}

void OtaManager::finish_install() {
    snap_.percent = 100;
    SN_LOG_NOTICE("ota", "install succeeded url=%s", log::quote(snap_.url).c_str());
    enter(OtaState::done);
}

void OtaManager::fail_install(const std::string& error) {
    snap_.error = error.empty() ? "unknown error" : error;
    SN_LOG_ERR("ota", "install failed: %s url=%s", snap_.error.c_str(), log::quote(snap_.url).c_str());
    enter(OtaState::failed);
}

void OtaManager::enter(OtaState s) {
    snap_.state = s;
    SN_LOG_INFO("ota", "state=%s", to_string(s));
    notify(OtaChange::state);
}

// SWUpdate gửi progress rất dày, chỉ báo client khi phần trăm đổi.
void OtaManager::update_percent(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (percent == snap_.percent) return;
    snap_.percent = percent;
    notify(OtaChange::progress);
}

void OtaManager::notify(OtaChange c) {
    if (on_change_) on_change_(c, snap_);
}

} // namespace sensornode::ota
