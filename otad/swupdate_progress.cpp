#include "otad/swupdate_progress.h"

#include "common/event_loop.h"
#include "common/log.h"

#include <progress_ipc.h>

#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sensornode::ota {

namespace {

using namespace std::chrono_literals;

constexpr auto kRetryPeriod = 500ms;

std::string field(const char* s, std::size_t n) {
    return std::string(s, strnlen(s, n));
}

} // namespace

ProgressClient::ProgressClient(EventLoop& loop, std::string path, const EventHandler& handler)
    : loop_(loop),
      path_(std::move(path)),
      handler_(handler),
      retry_(loop, [this](std::uint64_t) { try_connect(); }) {}

ProgressClient::~ProgressClient() {
    disconnect();
}

void ProgressClient::start() {
    stop();
    last_step_ = 0;
    try_connect();
    if (fd_ < 0) retry_.start_periodic(kRetryPeriod);
}

void ProgressClient::stop() {
    retry_.stop();
    disconnect();
}

void ProgressClient::try_connect() {
    const int fd = progress_ipc_connect_with_path(path_.c_str(), false);
    if (fd < 0) return;

    retry_.stop();
    fd_ = fd;
    loop_.add(fd_, EPOLLIN, [this](std::uint32_t) { on_readable(); });
    SN_LOG_INFO("swupdate", "progress connected path=%s", path_.c_str());
}

void ProgressClient::on_readable() {
    const int fd = fd_;

    progress_msg msg{};
    const int n = progress_ipc_receive(&fd_, &msg);
    if (n > 0) {
        dispatch(msg);
        return;
    }
    if (n == 0) return;

    SN_LOG_DEBUG("swupdate", "progress disconnected ret=%d", n);
    loop_.remove(fd);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void ProgressClient::dispatch(const progress_msg& m) {
    switch (m.status) {
    case DOWNLOAD:
        handler_({EventType::download_progress, static_cast<int>(std::min(m.dwl_percent, 100u))});
        break;

    case RUN:
    case PROGRESS: {
        if (m.nsteps == 0 || m.cur_step == 0) return;
        const unsigned step = std::min(m.cur_step, m.nsteps);
        if (step != last_step_) {
            last_step_ = step;
            SN_LOG_INFO("swupdate", "step %u/%u image=%s handler=%s", step, m.nsteps,
                        field(m.cur_image, sizeof m.cur_image).c_str(),
                        field(m.hnd_name, sizeof m.hnd_name).c_str());
        }
        const unsigned pct = ((step - 1) * 100 + std::min(m.cur_percent, 100u)) / m.nsteps;
        handler_({EventType::install_progress, static_cast<int>(pct)});
        break;
    }

    default:
        SN_LOG_DEBUG("swupdate", "progress status=%u info=%s", static_cast<unsigned>(m.status),
                     log::quote(field(m.info, std::min<std::size_t>(m.infolen, sizeof m.info))).c_str());
        break;
    }
}

void ProgressClient::disconnect() {
    if (fd_ < 0) return;
    loop_.remove(fd_);
    ::close(fd_);
    fd_ = -1;
}

} // namespace sensornode::ota
