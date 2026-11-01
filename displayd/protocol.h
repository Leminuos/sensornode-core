#pragma once

#include <functional>
#include <string>

namespace sensornode::display {

// Trả về false nếu ghi xuống phần cứng lỗi.
using BrightnessSetter = std::function<bool(int value)>;

// Message gửi ngay khi client kết nối
std::string make_hello();

// Xử lý một dòng lệnh từ client và trả về dòng reply
std::string handle_command(const std::string& line, const BrightnessSetter& set_brightness);

} // namespace sensornode::display
