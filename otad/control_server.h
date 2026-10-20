#pragma once

#include "common/unix_server.h"
#include "otad/protocol.h"

#include <string>

namespace sensornode::ota {

// Nhận lệnh trên control socket, thực thi rồi gửi reply.
class ControlServer {
public:
    ControlServer(EventLoop& loop, const std::string& path);


private:
    std::string execute(const Command& cmd);

    UnixServer        server_;
};

} // namespace sensornode::ota
