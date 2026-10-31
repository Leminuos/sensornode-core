#pragma once

#include <string>

namespace sensornode {

// MAC của interface vật lý đầu tiên theo tên, dạng 12 ký tự hex. Rỗng nếu không có.
std::string device_id_from_mac(const std::string& net_dir = "/sys/class/net");

// Mặc định của --node-id: MAC, không đọc được thì hostname kèm cảnh báo trên stderr.
std::string default_device_id();

} // namespace sensornode
