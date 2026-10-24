#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sensornode::ota {

struct Manifest {
    std::string                  version;
    std::string                  url;
    std::optional<std::uint64_t> size;
    std::string                  sha256;   // rỗng nếu manifest không có

    bool operator==(const Manifest& o) const {
        return version == o.version && url == o.url && size == o.size && sha256 == o.sha256;
    }
};

// Trả về nullopt và điền err nếu payload không phải manifest hợp lệ.
std::optional<Manifest> parse_manifest(std::string_view payload, std::string& err);

// So từng cụm số theo dấu chấm, "0.10.0" > "0.9.1"; < 0, 0, > 0 như strcmp.
int compare_versions(std::string_view a, std::string_view b);

// http(s) url không có khoảng trắng và ký tự điều khiển.
bool valid_url(std::string_view url);

} // namespace sensornode::ota
