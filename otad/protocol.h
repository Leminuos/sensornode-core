#pragma once

#include "otad/ota_manager.h"
#include "otad/system_info.h"

#include <string>

namespace sensornode::ota {

// Message gửi ngay khi client kết nối
std::string make_hello(const OtaManager::Snapshot& ota);

// Message broadcast khi trạng thái OTA đổi.
std::string make_state_event(const OtaManager::Snapshot& ota);

// Message broadcast khi phần trăm tải/cài đổi.
std::string make_progress_event(const OtaManager::Snapshot& ota);

// Xử lý một dòng lệnh từ client và trả về dòng reply
std::string handle_command(const std::string& line, const SystemInfo& sys, OtaManager& ota);

} // namespace sensornode::ota
