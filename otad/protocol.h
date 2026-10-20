#pragma once

#include <string>

namespace sensornode::ota {

// Message gửi ngay khi client kết nối
std::string make_hello();

// Xử lý một dòng lệnh từ client và trả về dòng reply
std::string handle_command(const std::string& line);

} // namespace sensornode::ota
