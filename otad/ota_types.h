#pragma once

#include "otad/manifest.h"

#include <functional>
#include <optional>
#include <string>

namespace sensornode::ota {

enum class OtaState { idle, downloading, installing, done, failed, rebooting };

const char* to_string(OtaState s);

enum class Error {
    none,
    invalid_json,
    missing_cmd,
    unknown_command,
    no_update_available,
    invalid_url,
    busy,
    reboot_pending,
    no_update_installed,
    already_rebooting,
    trial_boot,
};

enum class EventType {
    install,
    reboot,
    download_progress,
    install_progress,
    install_ok,
    install_failed,
    reboot_failed,
};

struct Event {
    EventType   type;
    int         percent = 0;
    std::string text    = {};
};

using EventHandler = std::function<void(const Event&)>;

struct Snapshot {
    OtaState    state   = OtaState::idle;
    int         percent = 0;
    std::string url;
    std::string error;
    std::optional<Manifest> available;
};

enum class OtaChange { state, progress, available };

} // namespace sensornode::ota
