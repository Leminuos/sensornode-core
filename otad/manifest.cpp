/*
 * Manifest firmware trên MQTT
 * ===========================
 *
 * Topic       ota/latest (mặc định), retained, do tools/ota-server/ota_server.py publish
 * Payload     cùng nội dung GET /manifest.json của OTA server
 *
 *   {"version":"0.3.0","url":"http://192.168.137.10:8000/release/x-0.3.0.swu",
 *    "size":47127040,"sha256":"075bcf..."}
 *
 *   +---------+--------+----------+------------------------------------------------+
 *   | key     | kiểu   | bắt buộc | kiểm tra                                       |
 *   +=========+========+==========+================================================+
 *   | version | string | có       | 1..64 ký tự [0-9A-Za-z.+_~-], có chữ số đầu    |
 *   | url     | string | có       | như lệnh install: http(s), không khoảng trắng  |
 *   | size    | int    | không    | >= 0                                           |
 *   | sha256  | string | không    | 64 ký tự hex                                   |
 *   +---------+--------+----------+------------------------------------------------+
 *
 *   Broker trong lab cho phép ai cũng publish, nên payload được coi là input không tin cậy.
 *   Chống image giả là việc của chữ ký .swu (sensornode-secureboot), không phải của manifest.
 */

#include "otad/manifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace sensornode::ota {

using nlohmann::json;

namespace {

constexpr std::size_t kMaxPayload    = 4096;
constexpr std::size_t kMaxVersionLen = 64;
constexpr std::size_t kMaxUrlLen     = 1024;

bool valid_version(const std::string& v) {
    if (v.empty() || v.size() > kMaxVersionLen) return false;
    if (!std::isdigit(static_cast<unsigned char>(v.front()))) return false;
    return std::all_of(v.begin(), v.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '+' || c == '_' || c == '~' || c == '-';
    });
}

bool valid_sha256(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isxdigit(c);
    });
}

// Số nguyên ở đầu cụm, "3-rc1" -> 3; cụm không có số coi là 0.
std::uint64_t leading_number(std::string_view part) {
    std::uint64_t n = 0;
    for (const char c : part) {
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        n = std::min<std::uint64_t>(n * 10 + static_cast<std::uint64_t>(c - '0'), 1'000'000'000);
    }
    return n;
}

std::string_view next_part(std::string_view& s) {
    const auto dot = s.find('.');
    const std::string_view part = s.substr(0, dot);
    s = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1);
    return part;
}

} // namespace

bool valid_url(std::string_view url) {
    if (url.empty() || url.size() > kMaxUrlLen) return false;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
    return std::none_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f; });
}

int compare_versions(std::string_view a, std::string_view b) {
    while (!a.empty() || !b.empty()) {
        const auto x = leading_number(next_part(a));
        const auto y = leading_number(next_part(b));
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::optional<Manifest> parse_manifest(std::string_view payload, std::string& err) {
    if (payload.size() > kMaxPayload) {
        err = "payload too large";
        return std::nullopt;
    }

    const json j = json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "invalid json";
        return std::nullopt;
    }

    Manifest m;

    const auto version = j.find("version");
    if (version == j.end() || !version->is_string() || !valid_version(version->get<std::string>())) {
        err = "invalid version";
        return std::nullopt;
    }
    m.version = version->get<std::string>();

    const auto url = j.find("url");
    if (url == j.end() || !url->is_string() || !valid_url(url->get_ref<const std::string&>())) {
        err = "invalid url";
        return std::nullopt;
    }
    m.url = url->get<std::string>();

    const auto size = j.find("size");
    if (size != j.end()) {
        if (!size->is_number_unsigned()) {
            err = "invalid size";
            return std::nullopt;
        }
        m.size = size->get<std::uint64_t>();
    }

    const auto sha = j.find("sha256");
    if (sha != j.end()) {
        if (!sha->is_string() || !valid_sha256(sha->get<std::string>())) {
            err = "invalid sha256";
            return std::nullopt;
        }
        m.sha256 = sha->get<std::string>();
        std::transform(m.sha256.begin(), m.sha256.end(), m.sha256.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    return m;
}

} // namespace sensornode::ota
