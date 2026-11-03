#include "displayd/protocol.h"

#include <nlohmann/json.hpp>

namespace sensornode::display {

using nlohmann::json;

namespace {

std::string to_line(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

json error_reply(json reply, const char* msg) {
    reply["ok"]    = false;
    reply["error"] = msg;
    return reply;
}

} // namespace

std::string make_hello() {
    return to_line({
        {"type",    "hello"},
        {"daemon",  "displayd"},
        {"version", SENSORNODE_VERSION},
    });
}

std::string handle_command(const std::string& line, const BrightnessSetter& set_brightness) {
    json reply = {{"type", "reply"}};

    const json req = json::parse(line, nullptr, false);
    if (req.is_discarded() || !req.is_object()) {
        return to_line(error_reply(std::move(reply), "invalid json"));
    }

    const auto id = req.find("id");
    if (id != req.end()) reply["id"] = *id;

    const auto cmd_it = req.find("cmd");
    if (cmd_it == req.end() || !cmd_it->is_string()) {
        return to_line(error_reply(std::move(reply), "missing cmd"));
    }

    const std::string& cmd = cmd_it->get_ref<const std::string&>();
    reply["cmd"] = cmd;

    if (cmd == "ping") {
        reply["ok"] = true;
    } else if (cmd == "set_brightness") {
        const auto value = req.find("value");
        if (value == req.end() || !value->is_number_integer()) {
            return to_line(error_reply(std::move(reply), "invalid value"));
        }
        const auto v = value->get<long long>();
        if (v < 0 || v > 100) return to_line(error_reply(std::move(reply), "invalid value"));

        if (!set_brightness(static_cast<int>(v))) {
            return to_line(error_reply(std::move(reply), "backlight write failed"));
        }
        reply["ok"] = true;
    } else {
        return to_line(error_reply(std::move(reply), "unknown command"));
    }

    return to_line(reply);
}

} // namespace sensornode::display
