#pragma once

#include "otad/ota_types.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sensornode::ota {

enum class CommandType { invalid, ping };

struct Command {
    CommandType    type  = CommandType::invalid;
    Error          error = Error::none;
    nlohmann::json reply;
};

Command parse_command(const std::string& line);

std::string make_reply(const Command& cmd, Error err);

std::string make_hello();

} // namespace sensornode::ota
