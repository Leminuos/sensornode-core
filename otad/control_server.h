#pragma once

#include "common/unix_server.h"
#include "otad/ota_manager.h"
#include "otad/protocol.h"
#include "otad/system_info.h"

#include <string>

namespace sensornode::ota {

// Nối control socket với OtaManager: nhận lệnh, thực thi, gửi reply và event.
class ControlServer {
public:
    ControlServer(EventLoop& loop, const std::string& path, const SystemInfo& sys, OtaManager& ota);

    void broadcast(OtaChange change, const Snapshot& ota);

private:
    std::string execute(const Command& cmd);

    UnixServer        server_;
    const SystemInfo& sys_;
    OtaManager&       ota_;
};

} // namespace sensornode::ota
