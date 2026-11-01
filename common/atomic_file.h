#pragma once

#include <string>
#include <string_view>

namespace sensornode {

// Ghi file tạm rồi rename để mất điện giữa chừng không làm hỏng file cũ.
// Trả về false và ghi lý do vào error nếu lỗi.
bool write_file_atomic(const std::string& path, std::string_view text, std::string& error);

} // namespace sensornode
