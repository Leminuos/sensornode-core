/*
 * Protocol của settingsd trên control socket
 * ==========================================
 *
 * Socket      /run/sensornode/settings.sock (mặc định)
 * Định dạng   JSON Lines: mỗi message là một object JSON trên một dòng.
 *
 *
 * 1. Thông số
 * -----------
 *
 *   +------------+------+----------+-----------------------------+
 *   | key        | kiểu | mặc định | giá trị                     |
 *   +============+======+==========+=============================+
 *   | brightness | int  | 72       | 0..100                      |
 *   | auto_mode  | bool | false    | tự bật popup khi có bản mới |
 *   +------------+------+----------+-----------------------------+
 *
 *
 * 2. Client -> daemon
 * -------------------
 *
 *   +------+----------------------------+
 *   | cmd  | Ý nghĩa                    |
 *   +======+============================+
 *   | ping | kiểm tra daemon còn sống   |
 *   | set  | đổi một thông số           |
 *   +------+----------------------------+
 *
 *   {"cmd":"set","id":2,"key":"brightness","value":50}
 *
 * 3. Daemon -> client
 * -------------------
 *
 *   hello
 *     {"type":"hello","daemon":"settingsd","version":"0.1.0",
 *      "settings":{"brightness":72,"auto_mode":false}}
 *
 *   reply
 *     {"type":"reply","cmd":"set","id":2,"ok":true}
 *     {"type":"reply","cmd":"set","id":3,"ok":false,"error":"invalid value"}
 *
 *
 * 4. Lỗi
 * ------
 *
 *   +-----------------+----------------------------------------+
 *   | error           | Nguyên nhân                            |
 *   +=================+========================================+
 *   | invalid json    | dòng không phải object JSON            |
 *   | missing cmd     | thiếu cmd hoặc không phải chuỗi        |
 *   | unknown command | cmd không được hỗ trợ                  |
 *   | unknown key     | key thiếu hoặc không có trong bảng 1   |
 *   | invalid value   | sai kiểu hoặc ngoài khoảng             |
 *   +-----------------+----------------------------------------+
 */

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
