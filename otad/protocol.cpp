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
 *   +-----------+----------------------+--------------------+----------------------+
 *   | type      | Khi nào              | Gửi tới            | Hàm                  |
 *   +===========+======================+====================+======================+
 *   | hello     | ngay sau khi kết nối | client vừa kết nối | make_hello           |
 *   | reply     | trả lời một lệnh     | client gửi lệnh    | handle_command       |
 *   | state     | đổi trạng thái OTA   | mọi client         | make_state_event     |
 *   | progress  | phần trăm thay đổi   | mọi client         | make_progress_event  |
 *   | available | bản mới thay đổi     | mọi client         | make_available_event |
 *   +-----------+----------------------+--------------------+----------------------+
 *
 *   Client nên lấy trạng thái từ hello rồi cập nhật theo các event, không cần poll.
 *
 *
 * 2. Client -> daemon
 * -------------------
 *
 *   +---------+--------------------------------------------------------------+
 *   | cmd     | Ý nghĩa                                                      |
 *   +=========+==============================================================+
 *   | ping    | kiểm tra daemon còn sống                                     |
 *   | status  | trạng thái OTA, version firmware và trạng thái A/B           |
 *   | install | cài .swu từ url, không có url thì cài bản ota.available      |
 *   +---------+--------------------------------------------------------------+
 *
 *   install
 *     {"cmd":"install","id":3}
 *     {"cmd":"install","id":3,"url":"http://192.168.137.10:8000/release/x-0.2.0.swu"}
 *
 *
 * 3. Daemon -> client
 * -------------------
 *
 *   ota (object dùng chung trong hello, status và mọi event)
 *     {"state":"downloading","percent":40,"url":"http://...","error":null,
 *      "available":{"version":"0.3.0","url":"http://...","size":47127040,"sha256":"075b..."}}
 *
 *   +-----------+-------------------------------------------------------------+
 *   | field     | Ý nghĩa                                                     |
 *   +===========+=============================================================+
 *   | state     | idle, downloading, installing, done, failed                 |
 *   | percent   | 0..100 khi downloading/installing, 100 ở done, còn lại null |
 *   | url       | url của lần cài gần nhất, null nếu chưa cài lần nào         |
 *   | error     | lý do lỗi khi state là failed, còn lại null                 |
 *   | available | manifest MQTT mới hơn sw_version, null nếu không có         |
 *   +-----------+-------------------------------------------------------------+
 *
 *   available độc lập với state: failed vẫn có thể có bản mới để cài lại.
 *
 *   hello
 *     {"type":"hello","daemon":"otad","version":"0.1.0","ota":{"state":"idle",...}}
 *
 *   state, progress
 *     {"type":"state","ota":{"state":"installing","percent":0,"url":"http://...","error":null}}
 *     {"type":"progress","ota":{"state":"installing","percent":30,"url":"http://...","error":null}}
 *     {"type":"state","ota":{"state":"failed","percent":null,"url":"http://...","error":"..."}}
 *
 *   reply
 *     {"type":"reply","cmd":"ping","id":1,"ok":true}
 *     {"type":"reply","cmd":"install","id":3,"ok":true}
 *     {"type":"reply","cmd":"install","id":4,"ok":false,"error":"busy"}
 *     {"type":"reply","cmd":"foo","ok":false,"error":"unknown command"}
 *
 *   status
 *     {"type":"reply","cmd":"status","ok":true,"ota":{"state":"idle",...},
 *      "system":{"sw_version":"0.2.0","running_slot":"A","active_slot":"A","ustate":0,
 *      "boot_count":0,"boot_limit":3,"errors":[]}}
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
 *   +-------------------------+------------------------------------------------+
 *   | error                   | Nguyên nhân                                    |
 *   +=========================+================================================+
 *   | invalid json            | dòng không phải object JSON                    |
 *   | missing cmd             | thiếu cmd hoặc không phải chuỗi                |
 *   | unknown command         | cmd không được hỗ trợ                          |
 *   | no update available     | install không có url và chưa có ota.available  |
 *   | invalid url             | không phải chuỗi http(s), có khoảng trắng, dài |
 *   | busy                    | đang downloading hoặc installing               |
 *   | reboot pending          | đã cài xong, phải reboot trước khi cài tiếp    |
 *   +-------------------------+------------------------------------------------+
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

json manifest_to_json(const std::optional<Manifest>& m) {
    if (!m) return nullptr;
    return {
        {"version", m->version},
        {"url",     m->url},
        {"size",    or_null(m->size)},
        {"sha256",  m->sha256.empty() ? json(nullptr) : json(m->sha256)},
    };
}

json ota_to_json(const OtaManager::Snapshot& s) {
    const bool running = s.state == OtaState::downloading || s.state == OtaState::installing;
    const bool has_percent = running || s.state == OtaState::done;

    return {
        {"state",     to_string(s.state)},
        {"percent",   has_percent ? json(s.percent) : json(nullptr)},
        {"url",       s.url.empty() ? json(nullptr) : json(s.url)},
        {"error",     s.state == OtaState::failed ? json(s.error) : json(nullptr)},
        {"available", manifest_to_json(s.available)},
    };
}

json error_reply(json reply, const char* msg) {
    reply["ok"]    = false;
    reply["error"] = msg;
    return reply;
}

} // namespace

std::string make_hello(const OtaManager::Snapshot& ota) {
    return to_line({
        {"type",    "hello"},
        {"daemon",  "otad"},
        {"version", SENSORNODE_VERSION},
        {"ota",     ota_to_json(ota)},
    });
}

std::string make_state_event(const OtaManager::Snapshot& ota) {
    return to_line({{"type", "state"}, {"ota", ota_to_json(ota)}});
}

std::string make_progress_event(const OtaManager::Snapshot& ota) {
    return to_line({{"type", "progress"}, {"ota", ota_to_json(ota)}});
}

std::string make_available_event(const OtaManager::Snapshot& ota) {
    return to_line({{"type", "available"}, {"ota", ota_to_json(ota)}});
}

std::string handle_command(const std::string& line, const SystemInfo& sys, OtaManager& ota) {
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
        reply["ota"]    = ota_to_json(ota.snapshot());
        reply["system"] = status_to_json(sys.read());
    } else if (cmd == "install") {
        // Không có url thì cài bản available từ manifest.
        std::string url;
        if (const auto it = req.find("url"); it != req.end()) {
            if (!it->is_string()) return to_line(error_reply(std::move(reply), "invalid url"));
            url = it->get<std::string>();
            if (url.empty()) return to_line(error_reply(std::move(reply), "invalid url"));
        }
        // Reply chỉ báo đã nhận lệnh, state event có thể đã được broadcast trước reply này.
        if (const char* err = ota.install(url)) {
            return to_line(error_reply(std::move(reply), err));
        }
        reply["ok"] = true;
    } else {
        return to_line(error_reply(std::move(reply), "unknown command"));
    }

    return to_line(reply);
}

} // namespace sensornode::ota
