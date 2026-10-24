#pragma once

#include "otad/ota_types.h"
#include "otad/system_info.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sensornode::ota {

enum class CommandType { invalid, ping, status, install, reboot };

struct Command {
    CommandType    type  = CommandType::invalid;
    Error          error = Error::none;
    std::string    url;
    nlohmann::json reply;
};

Command parse_command(const std::string& line);

std::string make_reply(const Command& cmd, Error err);

std::string make_status_reply(const Command& cmd, const Snapshot& ota, const SystemStatus& sys);

std::string make_hello(const Snapshot& ota);

std::string make_event(OtaChange change, const Snapshot& ota);

} // namespace sensornode::ota
