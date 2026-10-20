#pragma once

#include "common/unix_server.h"
#include "otad/protocol.h"
#include "otad/system_info.h"

#include <string>

namespace sensornode::ota {

// Nhận lệnh trên control socket, thực thi rồi gửi reply.
class ControlServer {
public:
    ControlServer(EventLoop& loop, const std::string& path, const SystemInfo& sys);


private:
    std::string execute(const Command& cmd);

    UnixServer        server_;
    const SystemInfo& sys_;
};

} // namespace sensornode::ota
