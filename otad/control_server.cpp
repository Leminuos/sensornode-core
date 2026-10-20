#include "otad/control_server.h"

#include "common/log.h"

#include <cinttypes>

namespace sensornode::ota {

namespace {

UnixServer::Options make_options(const std::string& path) {
    UnixServer::Options opts;
    opts.path = path;
    return opts;
}

} // namespace

ControlServer::ControlServer(EventLoop& loop, const std::string& path, const SystemInfo& sys)
    : server_(loop, make_options(path)), sys_(sys) {
    SN_LOG_INFO("ipc", "listening path=%s", path.c_str());

    server_.on_connect([this](UnixServer::ClientId id, const PeerCred& p) {
        SN_LOG_INFO("ipc", "client connected cid=%" PRIu64 " pid=%d uid=%d gid=%d", id,
                    static_cast<int>(p.pid), static_cast<int>(p.uid), static_cast<int>(p.gid));
        server_.send(id, make_hello());
        return true;
    });

    server_.on_disconnect([](UnixServer::ClientId id, const char* reason) {
        SN_LOG_INFO("ipc", "client disconnected: %s cid=%" PRIu64, reason, id);
    });

    server_.on_line([this](UnixServer::ClientId id, const std::string& line) {
        SN_LOG_DEBUG("ipc", "line received cid=%" PRIu64 " line=%s", id, log::quote(line).c_str());
        server_.send(id, execute(parse_command(line)));
    });
}

std::string ControlServer::execute(const Command& cmd) {
    switch (cmd.type) {
    case CommandType::invalid: return make_reply(cmd, cmd.error);
    case CommandType::ping:    return make_reply(cmd, Error::none);
    case CommandType::status:  return make_status_reply(cmd, sys_.read());
    }
    return make_reply(cmd, Error::unknown_command);
}

} // namespace sensornode::ota
