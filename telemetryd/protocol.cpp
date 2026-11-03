#include "telemetryd/protocol.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>

namespace sensornode {

using nlohmann::json;

namespace {

json reading(double v, bool ok) {
    if (!ok) return nullptr;
    return std::round(v * 100.0) / 100.0;
}

json sample_to_json(const Sample& s) {
    json errors = json::array();
    for (const auto& e : s.errors) {
        errors.push_back({{"sensor", e.sensor}, {"error", std::strerror(-e.err)}});
    }

    return {
        {"ts",      s.ts_ms},
        {"temp_c",  reading(s.temp_c,  !s.failed(kSht30))},
        {"humi_rh", reading(s.humi_rh, !s.failed(kSht30))},
        {"lux",     reading(s.lux,     !s.failed(kBh1750))},
        {"errors",  std::move(errors)},
    };
}

// Chuyển json thành một dòng gửi qua socket.
std::string to_line(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

json error_reply(json reply, const char* msg) {
    reply["ok"]    = false;
    reply["error"] = msg;
    return reply;
}

} // namespace

std::string make_hello(std::chrono::milliseconds interval, const std::string& node,
                       const std::string& mac) {
    return to_line({
        {"type",        "hello"},
        {"daemon",      "telemetryd"},
        {"version",     SENSORNODE_VERSION},
        {"interval_ms", interval.count()},
        {"node",        node},
        {"mac",         mac.empty() ? json(nullptr) : json(mac)},
    });
}

std::string make_sample_event(const Sample& s) {
    json j = sample_to_json(s);
    j["type"] = "sample";
    return to_line(j);
}

std::string make_sample_payload(const Sample& s) {
    return to_line(sample_to_json(s));
}

std::string handle_command(const std::string& line, const Sample* latest) {
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
    } else if (cmd == "get_latest") {
        if (!latest) return to_line(error_reply(std::move(reply), "no sample yet"));
        reply["ok"]     = true;
        reply["sample"] = sample_to_json(*latest);
    } else {
        return to_line(error_reply(std::move(reply), "unknown command"));
    }

    return to_line(reply);
}

} // namespace sensornode
