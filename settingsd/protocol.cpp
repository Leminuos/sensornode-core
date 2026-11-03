#include "settingsd/protocol.h"

namespace sensornode::settings {

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

std::string make_hello(const Store& store) {
    return to_line({
        {"type",     "hello"},
        {"daemon",   "settingsd"},
        {"version",  SENSORNODE_VERSION},
        {"settings", store.values()},
    });
}

std::string handle_command(const std::string& line, Store& store) {
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
    } else if (cmd == "set") {
        const auto key   = req.find("key");
        const auto value = req.find("value");
        if (key == req.end() || !key->is_string()) {
            return to_line(error_reply(std::move(reply), "unknown key"));
        }
        if (value == req.end()) return to_line(error_reply(std::move(reply), "invalid value"));

        if (const char* err = store.set(key->get<std::string>(), *value)) {
            return to_line(error_reply(std::move(reply), err));
        }
        reply["ok"] = true;
    } else {
        return to_line(error_reply(std::move(reply), "unknown command"));
    }

    return to_line(reply);
}

} // namespace sensornode::settings
