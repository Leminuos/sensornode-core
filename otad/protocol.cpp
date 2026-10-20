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
    case Error::invalid_url:         return "invalid url";
    case Error::busy:                return "busy";
    case Error::reboot_pending:      return "reboot pending";
    case Error::trial_boot:          return "trial boot";
    }
    return "unknown error";
}

const char* to_string(OtaChange c) {
    switch (c) {
    case OtaChange::state:     return "state";
    case OtaChange::progress:  return "progress";
    }
    return "unknown";
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

json ota_to_json(const Snapshot& s) {
    const bool running = s.state == OtaState::downloading || s.state == OtaState::installing;
    const bool has_percent = running || s.state == OtaState::done;

    return {
        {"state",     to_string(s.state)},
        {"percent",   has_percent ? json(s.percent) : json(nullptr)},
        {"url",       s.url.empty() ? json(nullptr) : json(s.url)},
        {"error",     s.error.empty() ? json(nullptr) : json(s.error)},
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
    } else if (cmd == "install") {
        const auto it = req.find("url");
        if (it == req.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
            return reject(std::move(c), Error::invalid_url);
        }
        c.url = it->get<std::string>();
        c.type = CommandType::install;
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

std::string make_status_reply(const Command& cmd, const Snapshot& ota, const SystemStatus& sys) {
    json reply = cmd.reply;
    reply["ok"]     = true;
    reply["ota"]    = ota_to_json(ota);
    reply["system"] = status_to_json(sys);
    return to_line(reply);
}

std::string make_hello(const Snapshot& ota) {
    return to_line({
        {"type",    "hello"},
        {"daemon",  "otad"},
        {"version", SENSORNODE_VERSION},
        {"ota",     ota_to_json(ota)},
    });
}

std::string make_event(OtaChange change, const Snapshot& ota) {
    return to_line({{"type", to_string(change)}, {"ota", ota_to_json(ota)}});
}

} // namespace sensornode::ota
