/*
 * Tích hợp systemd không cần libsystemd.
 *
 * notify: gửi một datagram tới Unix socket có địa chỉ trong $NOTIFY_SOCKET.
 * Mỗi datagram gồm các dòng KEY=VALUE, ví dụ:
 *   READY=1\nSTATUS=measuring every 5000ms
 *   WATCHDOG=1
 *   STOPPING=1
 *
 * Watchdog: systemd đặt WATCHDOG_USEC (và WATCHDOG_PID) khi unit có WatchdogSec=.
 * Daemon phải gửi WATCHDOG=1 trước khi hết hạn, ở đây ping mỗi nửa chu kỳ
 * và chỉ ping khi health check của daemon trả về true.
 */

#include "common/systemd.h"
#include "common/log.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace sensornode {

namespace systemd {

bool notify(std::string_view state) {
    const char* path = std::getenv("NOTIFY_SOCKET");
    if (!path || !*path) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::size_t len = std::strlen(path);
    if ((path[0] != '/' && path[0] != '@') || len >= sizeof addr.sun_path) {
        SN_LOG_WARN("systemd", "unsupported NOTIFY_SOCKET value=%s", log::quote(path).c_str());
        return false;
    }
    std::memcpy(addr.sun_path, path, len);
    if (addr.sun_path[0] == '@') addr.sun_path[0] = '\0';
    const auto addr_len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + len);

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        SN_LOG_WARN("systemd", "notify failed: socket: %s", log::errstr(errno).c_str());
        return false;
    }

    const ssize_t n = sendto(fd, state.data(), state.size(), MSG_NOSIGNAL,
                             reinterpret_cast<const sockaddr*>(&addr), addr_len);
    const int err = errno;
    close(fd);

    if (n != static_cast<ssize_t>(state.size())) {
        SN_LOG_WARN("systemd", "notify failed: sendto: %s", log::errstr(err).c_str());
        return false;
    }
    return true;
}

std::chrono::microseconds watchdog_timeout() {
    const char* usec = std::getenv("WATCHDOG_USEC");
    if (!usec) return std::chrono::microseconds{0};

    if (const char* pid = std::getenv("WATCHDOG_PID")) {
        if (std::strtol(pid, nullptr, 10) != getpid()) return std::chrono::microseconds{0};
    }

    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(usec, &end, 10);
    if (errno != 0 || end == usec || *end != '\0' || v <= 0) {
        SN_LOG_WARN("systemd", "invalid WATCHDOG_USEC value=%s", log::quote(usec).c_str());
        return std::chrono::microseconds{0};
    }
    return std::chrono::microseconds{v};
}

} // namespace systemd

Watchdog::Watchdog(EventLoop& loop, HealthCheck healthy)
    : healthy_(std::move(healthy)),
      timeout_(systemd::watchdog_timeout()),
      timer_(loop, [this](std::uint64_t) { on_tick(); }) {
    if (!enabled()) {
        SN_LOG_INFO("systemd", "watchdog disabled");
        return;
    }
    SN_LOG_INFO("systemd", "watchdog enabled timeout=%lldms",
                static_cast<long long>(timeout_.count() / 1000));
    timer_.start_periodic(timeout_ / 2);
}

void Watchdog::on_tick() {
    const bool ok = !healthy_ || healthy_();
    if (ok != was_healthy_) {
        if (ok) SN_LOG_NOTICE("systemd", "health check recovered, watchdog ping resumed");
        else    SN_LOG_ERR("systemd", "health check failed, watchdog ping withheld");
        was_healthy_ = ok;
    }
    if (ok) systemd::notify("WATCHDOG=1");
}

} // namespace sensornode
