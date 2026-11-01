#pragma once

#include "settingsd/store.h"

#include <string>

namespace sensornode::settings {

// Message gửi ngay khi client kết nối
std::string make_hello(const Store& store);

// Xử lý một dòng lệnh từ client và trả về dòng reply
std::string handle_command(const std::string& line, Store& store);

} // namespace sensornode::settings
