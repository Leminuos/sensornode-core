/*
 * Protocol của telemetryd trên control socket
 * ===========================================
 *
 * Socket      /run/sensornode/telemetry.sock (mặc định)
 * Định dạng   JSON Lines: mỗi message là một object JSON trên một dòng.
 *
 *
 * 1. Luồng message
 * ----------------
 * 
 *   Ba loại message daemon gửi đi:
 *
 *   +--------+----------------------+--------------------+-------------------+
 *   | type   | Khi nào              | Gửi tới            | Hàm               |
 *   +========+======================+====================+===================+
 *   | hello  | ngay sau khi kết nối | client vừa kết nối | make_hello        |
 *   | sample | mỗi lần đo xong      | mọi client         | make_sample_event |
 *   | reply  | trả lời một lệnh     | client gửi lệnh    | handle_command    |
 *   +--------+----------------------+--------------------+-------------------+
 *
 *
 * 2. Client -> daemon
 * -------------------
 * 
 *   +------------+--------------------------+
 *   | cmd        | Ý nghĩa                  |
 *   +============+==========================+
 *   | ping       | kiểm tra daemon còn sống |
 *   | get_latest | lấy lần đo gần nhất      |
 *   +------------+--------------------------+
 *
 *
 * 3. Daemon -> client
 * -------------------
 * 
 *   hello
 *     {"type":"hello","daemon":"telemetryd","version":"0.1.0","interval_ms":5000}
 *
 *   sample
 *     {"type":"sample","ts":1792297659621,"temp_c":24.16,"humi_rh":54.48,"lux":310.47,"errors":[]}
 *
 *   sample khi cảm biến lỗi
 *     {"type":"sample",...,"temp_c":null,"humi_rh":null, "errors":[{"sensor":"sht30","error":"<strerror>"}]}
 *
 *   reply
 *     {"type":"reply","cmd":"ping","id":1,"ok":true}
 *     {"type":"reply","cmd":"get_latest","id":"1","ok":true,"sample":{ts, temp_c, ..., errors}}
 *     {"type":"reply","cmd":"foo","ok":false,"error":"unknown command"}
 *
 *
 * 4. Lỗi
 * ------
 * 
 *   +-----------------+-----------------------------------+
 *   | error           | Nguyên nhân                       |
 *   +=================+===================================+
 *   | invalid json    | dòng không phải object JSON       |
 *   | missing cmd     | thiếu cmd hoặc không phải chuỗi   |
 *   | unknown command | cmd không được hỗ trợ             |
 *   | no sample yet   | get_latest khi chưa đo lần nào    |
 *   +-----------------+-----------------------------------+
 *
 *
 * 5. MQTT
 * -------
 *
 *   +--------------------------------+-----+--------+----------------------------------+
 *   | Topic                          | QoS | Retain | Payload                          |
 *   +================================+=====+========+==================================+
 *   | sensornode/<node>/status       | 1   | có     | online / offline (LWT)           |
 *   | sensornode/<node>/telemetry    | 1   | không  | sample không có type             |
 *   +--------------------------------+-----+--------+----------------------------------+
 *
 *   telemetry
 *     {"ts":1792297659621,"temp_c":24.16,"humi_rh":54.48,"lux":310.47,"errors":[]}
 */

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

std::string make_hello(std::chrono::milliseconds interval) {
    return to_line({
        {"type",        "hello"},
        {"daemon",      "telemetryd"},
        {"version",     SENSORNODE_VERSION},
        {"interval_ms", interval.count()},
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
