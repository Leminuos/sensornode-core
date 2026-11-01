#include "common/unix_client.h"

#include "common/event_loop.h"
#include "common/log.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace sensornode {

namespace {

using namespace std::chrono_literals;

constexpr auto        kRetryDelay = 2s;
constexpr std::size_t kMaxLine    = 64 * 1024;

} // namespace

UnixClient::UnixClient(EventLoop& loop, std::string path)
    : loop_(loop),
      path_(std::move(path)),
      retry_timer_(loop, [this](std::uint64_t) { try_connect(); }) {}

UnixClient::~UnixClient() {
    drop();
}

void UnixClient::start() {
    try_connect();
}

void UnixClient::stop() {
    retry_timer_.stop();
    drop();
    in_.clear();
}

void UnixClient::try_connect() {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path_.size() >= sizeof(addr.sun_path)) {
        SN_LOG_ERR("ipc", "socket path too long path=%s", path_.c_str());
        return;
    }
    std::memcpy(addr.sun_path, path_.c_str(), path_.size());

    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0 || ::connect(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (!warned_) {
            SN_LOG_INFO("ipc", "cannot connect, will retry: %s path=%s",
                        log::errstr(errno).c_str(), path_.c_str());
            warned_ = true;
        }
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        retry_timer_.start_oneshot(kRetryDelay);
        return;
    }

    warned_ = false;
    in_.clear();
    loop_.add(fd_, EPOLLIN, [this](std::uint32_t) { on_readable(); });
    SN_LOG_INFO("ipc", "connected path=%s", path_.c_str());
}

void UnixClient::on_readable() {
    char buf[4096];
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
    if (n <= 0) {
        SN_LOG_INFO("ipc", "disconnected path=%s", path_.c_str());
        drop();
        retry_timer_.start_oneshot(kRetryDelay);
        return;
    }

    in_.append(buf, static_cast<std::size_t>(n));

    std::size_t nl;
    while ((nl = in_.find('\n')) != std::string::npos) {
        const std::string line = in_.substr(0, nl);
        in_.erase(0, nl + 1);
        if (on_line_) on_line_(line);
    }

    if (in_.size() > kMaxLine) {
        SN_LOG_WARN("ipc", "line too long, dropped path=%s", path_.c_str());
        in_.clear();
    }
}

void UnixClient::drop() {
    if (fd_ < 0) return;
    loop_.remove(fd_);
    ::close(fd_);
    fd_ = -1;
}

} // namespace sensornode
