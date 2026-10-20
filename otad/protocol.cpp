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
 *   +------+--------------------------+
 *   | cmd  | Ý nghĩa                  |
 *   +======+==========================+
 *   | ping | kiểm tra daemon còn sống |
 *   +------+--------------------------+
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

std::string handle_command(const std::string& line) {
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
    } else {
        return to_line(error_reply(std::move(reply), "unknown command"));
    }

    return to_line(reply);
}

} // namespace sensornode::ota
