/*
 * Protocol của otad trên control socket
 * =====================================
 *
 * Socket      /run/sensornode/ota.sock (mặc định)
 * Định dạng   JSON Lines: mỗi message là một object JSON trên một dòng.
 *
 *
 * 1. Luồng message
 * ----------------
 *
 *   +--------+----------------------+--------------------+----------------+
 *   | type   | Khi nào              | Gửi tới            | Hàm            |
 *   +========+======================+====================+================+
 *   | hello  | ngay sau khi kết nối | client vừa kết nối | make_hello     |
 *   | reply  | trả lời một lệnh     | client gửi lệnh    | handle_command |
 *   +--------+----------------------+--------------------+----------------+
 *
 *
 * 2. Client -> daemon
 * -------------------
 *
 *   +--------+------------------------------------+
 *   | cmd    | Ý nghĩa                            |
 *   +========+====================================+
 *   | ping   | kiểm tra daemon còn sống           |
 *   | status | version firmware và trạng thái A/B |
 *   +--------+------------------------------------+
 *
 *
 * 3. Daemon -> client
 * -------------------
 *
 *   hello
 *     {"type":"hello","daemon":"otad","version":"0.1.0"}
 *
 *   reply
 *     {"type":"reply","cmd":"ping","id":1,"ok":true}
 *     {"type":"reply","cmd":"foo","ok":false,"error":"unknown command"}
 *
 *   status
 *     {"type":"reply","cmd":"status","ok":true,"system":{"sw_version":"0.2.0",
 *      "running_slot":"A","active_slot":"A","ustate":0,"boot_count":0,"boot_limit":3,"errors":[]}}
 *
 *   status khi không đọc được một nguồn:
 *     {...,"system":{...,"active_slot":null,"ustate":null,...,
 *      "errors":[{"source":"bootenv","error":"open: No such file or directory"}]}}
 *
 *   +--------------+------------------------------------------+
 *   | field        | Nguồn                                    |
 *   +==============+==========================================+
 *   | sw_version   | /etc/sw-versions                         |
 *   | running_slot | root= trong /proc/cmdline                |
 *   | active_slot  | U-Boot env, slot sẽ boot lần tới         |
 *   | ustate       | U-Boot env, 1 = đang boot thử bản mới    |
 *   | boot_count   | U-Boot env, số lần đã boot thử           |
 *   | boot_limit   | U-Boot env, boot thử quá số này thì lùi  |
 *   +--------------+------------------------------------------+
 *
 *   running_slot khác active_slot nghĩa là đã cài xong bản mới, đang chờ reboot.
 *
 *
 * 4. Lỗi
 * ------
 *
 *   +-----------------+---------------------------------+
 *   | error           | Nguyên nhân                     |
 *   +=================+=================================+
 *   | invalid json    | dòng không phải object JSON     |
 *   | missing cmd     | thiếu cmd hoặc không phải chuỗi |
 *   | unknown command | cmd không được hỗ trợ           |
 *   +-----------------+---------------------------------+
 */

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

json error_reply(json reply, const char* msg) {
    reply["ok"]    = false;
    reply["error"] = msg;
    return reply;
}

} // namespace

std::string make_hello() {
    return to_line({
        {"type",    "hello"},
        {"daemon",  "otad"},
        {"version", SENSORNODE_VERSION},
    });
}

std::string handle_command(const std::string& line, const SystemInfo& sys) {
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
    } else if (cmd == "status") {
        reply["ok"]     = true;
        reply["system"] = status_to_json(sys.read());
    } else {
        return to_line(error_reply(std::move(reply), "unknown command"));
    }

    return to_line(reply);
}

} // namespace sensornode::ota
