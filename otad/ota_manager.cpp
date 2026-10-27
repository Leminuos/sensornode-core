/*
 * Máy trạng thái OTA
 * ==================
 *
 *   +------+  install   +-------------+  install_progress  +------------+  install_ok  +------+
 *   | idle | ---------> | downloading | -----------------> | installing | -----------> | done |
 *   +------+            +-------------+                    +------------+              +------+
 *                          ^     |                               |                        |  ^
 *                          |     | install_failed                | install_failed  reboot |  | reboot_failed
 *                  install |     v                               |                        v  |
 *                          | +--------+                          |                   +-----------+
 *                          +-| failed | <------------------------+                   | rebooting |
 *                            +--------+                                              +-----------+
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
                       const SystemInfo& sys, const SystemStatus& status)
    : installer_(std::move(installer)),
      rebooter_(std::move(rebooter)),
      sys_(sys),
      running_version_(status.sw_version) {
    if (status.reboot_pending()) {
        snap_.state   = OtaState::done;
        snap_.percent = 100;
    }

    installer_->on_event([this](const Event& e) { dispatch(e); });
    rebooter_->on_event([this](const Event& e) { dispatch(e); });
}

Error OtaManager::dispatch(const Event& e) {
    switch (snap_.state) {
    case OtaState::idle:        return on_idle(e);
    case OtaState::downloading: return on_downloading(e);
    case OtaState::installing:  return on_installing(e);
    case OtaState::done:        return on_done(e);
    case OtaState::failed:      return on_failed(e);
    case OtaState::rebooting:   return on_rebooting(e);
    }
    return Error::none;
}

Error OtaManager::on_idle(const Event& e) {
    switch (e.type) {
    case EventType::install: return begin_install(e.text);
    case EventType::reboot:  return Error::no_update_installed;
    case EventType::download_progress:
    case EventType::install_progress:
    case EventType::install_ok:
    case EventType::install_failed:
    case EventType::reboot_failed:
        break;
    }
    return Error::none;
}

Error OtaManager::on_downloading(const Event& e) {
    switch (e.type) {
    case EventType::install:
    case EventType::reboot:
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
    case EventType::reboot_failed:
        break;
    }
    return Error::none;
}

Error OtaManager::on_installing(const Event& e) {
    switch (e.type) {
    case EventType::install:
    case EventType::reboot:
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
    case EventType::reboot_failed:
        break;
    }
    return Error::none;
}

Error OtaManager::on_done(const Event& e) {
    switch (e.type) {
    case EventType::install:
        return Error::reboot_pending;
    case EventType::reboot:
        begin_reboot();
        break;
    case EventType::download_progress:
    case EventType::install_progress:
    case EventType::install_ok:
    case EventType::install_failed:
    case EventType::reboot_failed:
        break;
    }
    return Error::none;
}

Error OtaManager::on_failed(const Event& e) {
    return on_idle(e);
}

Error OtaManager::on_rebooting(const Event& e) {
    switch (e.type) {
    case EventType::install:
        return Error::reboot_pending;
    case EventType::reboot:
        return Error::already_rebooting;
    case EventType::reboot_failed:
        fail_reboot(e.text);
        break;
    case EventType::download_progress:
    case EventType::install_progress:
    case EventType::install_ok:
    case EventType::install_failed:
        break;
    }
    return Error::none;
}

Error OtaManager::begin_install(const std::string& url) {
    if (url.empty() && !snap_.available) return Error::no_update_available;
    const std::string target = url.empty() ? snap_.available->url : url;
    if (!valid_url(target)) return Error::invalid_url;
    if (sys_.read().trial_boot()) return Error::trial_boot;

    snap_.url     = target;
    snap_.error.clear();
    snap_.percent = 0;
    SN_LOG_NOTICE("ota", "install started url=%s", log::quote(target).c_str());
    enter(OtaState::downloading);

    std::string error;
    if (!installer_->start(target, error)) fail_install(error);
    return Error::none;
}

void OtaManager::begin_reboot() {
    snap_.error.clear();
    SN_LOG_NOTICE("ota", "reboot requested");
    enter(OtaState::rebooting);

    std::string error;
    if (!rebooter_->start(error)) fail_reboot(error);
}

void OtaManager::finish_install() {
    snap_.percent = 100;
    SN_LOG_NOTICE("ota", "install succeeded url=%s", log::quote(snap_.url).c_str());
    if (snap_.available && snap_.available->url == snap_.url) snap_.available.reset();
    enter(OtaState::done);
}

void OtaManager::fail_install(const std::string& error) {
    snap_.error = error.empty() ? "unknown error" : error;
    SN_LOG_ERR("ota", "install failed: %s url=%s", snap_.error.c_str(), log::quote(snap_.url).c_str());
    enter(OtaState::failed);
}

void OtaManager::fail_reboot(const std::string& error) {
    snap_.error = "reboot failed: " + error;
    SN_LOG_ERR("ota", "%s", snap_.error.c_str());
    enter(OtaState::done);
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

void OtaManager::offer(std::optional<Manifest> m) {
    if (m && force_update_) {
        SN_LOG_INFO("ota", "manifest accepted (force-update) version=%s running=%s",
                    m->version.c_str(), running_version_ ? running_version_->c_str() : "?");
    } else if (m) {
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
    notify(OtaChange::available);
}

void OtaManager::notify(OtaChange c) {
    if (on_change_) on_change_(c, snap_);
}

} // namespace sensornode::ota
