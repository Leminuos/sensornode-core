#pragma once

#include <functional>
#include <string>

namespace sensornode::ota {

enum class OtaState { idle, downloading, installing, done, failed };

const char* to_string(OtaState s);

enum class Error {
    none,
    invalid_json,
    missing_cmd,
    unknown_command,
    invalid_url,
    busy,
    reboot_pending,
    trial_boot,
};

enum class EventType {
    install,
    download_progress,
    install_progress,
    install_ok,
    install_failed,
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
};

enum class OtaChange { state, progress };

} // namespace sensornode::ota
