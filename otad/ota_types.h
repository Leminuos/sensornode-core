#pragma once

namespace sensornode::ota {

enum class Error {
    none,
    invalid_json,
    missing_cmd,
    unknown_command,
};

} // namespace sensornode::ota
