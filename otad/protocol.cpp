#include "otad/protocol.h"

#include <nlohmann/json.hpp>

namespace sensornode::ota {

using nlohmann::json;

namespace {

std::string to_line(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

template <typename T>
json or_null(const std::optional<T>& v) {
    return v ? json(*v) : json(nullptr);
}

const char* to_string(Error e) {
    switch (e) {
    case Error::none:                return "";
    case Error::invalid_json:        return "invalid json";
    case Error::missing_cmd:         return "missing cmd";
    case Error::unknown_command:     return "unknown command";
    }
    return "unknown error";
}

json status_to_json(const SystemStatus& st) {
    json errors = json::array();
    for (const auto& e : st.errors) {
        errors.push_back({{"source", e.source}, {"error", e.message}});
    }

    return {
        {"sw_version",   or_null(st.sw_version)},
        {"running_slot", or_null(st.running_slot)},
        {"active_slot",  or_null(st.active_slot)},
        {"ustate",       or_null(st.ustate)},
        {"boot_count",   or_null(st.boot_count)},
        {"boot_limit",   or_null(st.boot_limit)},
        {"errors",       std::move(errors)},
    };
}

Command reject(Command c, Error err) {
    c.type  = CommandType::invalid;
    c.error = err;
    return c;
}

} // namespace

Command parse_command(const std::string& line) {
    Command c;
    c.reply = {{"type", "reply"}};

    const json req = json::parse(line, nullptr, false);
    if (req.is_discarded() || !req.is_object()) return reject(std::move(c), Error::invalid_json);

    const auto id = req.find("id");
    if (id != req.end()) c.reply["id"] = *id;

    const auto cmd_it = req.find("cmd");
    if (cmd_it == req.end() || !cmd_it->is_string()) return reject(std::move(c), Error::missing_cmd);

    const std::string& cmd = cmd_it->get_ref<const std::string&>();
    c.reply["cmd"] = cmd;

    if (cmd == "ping") {
        c.type = CommandType::ping;
    } else if (cmd == "status") {
        c.type = CommandType::status;
    } else {
        return reject(std::move(c), Error::unknown_command);
    }
    return c;
}

std::string make_reply(const Command& cmd, Error err) {
    json reply = cmd.reply;
    reply["ok"] = err == Error::none;
    if (err != Error::none) reply["error"] = to_string(err);
    return to_line(reply);
}

std::string make_status_reply(const Command& cmd, const SystemStatus& sys) {
    json reply = cmd.reply;
    reply["ok"]     = true;
    reply["system"] = status_to_json(sys);
    return to_line(reply);
}

std::string make_hello() {
    return to_line({
        {"type",    "hello"},
        {"daemon",  "otad"},
        {"version", SENSORNODE_VERSION},
    });
}

} // namespace sensornode::ota
