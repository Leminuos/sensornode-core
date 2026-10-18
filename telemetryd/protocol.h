#pragma once

#include "telemetryd/sensors.h"

#include <chrono>
#include <string>

namespace sensornode {

// Message gửi ngay khi client kết nối
std::string make_hello(std::chrono::milliseconds interval);

// Message broadcast sau mỗi lần đo.
std::string make_sample_event(const Sample& s);

// Xử lý một dòng lệnh từ client và trả về dòng reply
std::string handle_command(const std::string& line, const Sample* latest);

} // namespace sensornode
