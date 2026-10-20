/*
 * Client cho progress socket của SWUpdate
 * =======================================
 *
 * Socket      /tmp/swupdateprog (CONFIG_SOCKET_PROGRESS_PATH), SOCK_STREAM
 * Định dạng   struct C nhị phân, cùng máy nên không cần đổi byte order
 *
 *   +----------------------+---------------+-----------------------------------+
 *   | Message              | Kích thước    | Khi nào                           |
 *   +======================+===============+===================================+
 *   | progress_connect_ack | 8 byte        | một lần, ngay sau khi accept      |
 *   | progress_msg         | 2416 byte     | mỗi lần trạng thái/phần trăm đổi  |
 *   +----------------------+---------------+-----------------------------------+
 *
 *   Stream không có đánh dấu ranh giới, một lần read có thể chứa nửa message hoặc
 *   nhiều message, nên gom vào in_ và chỉ cắt khi đủ sizeof.
 *
 * Map sang Phase
 *
 *   +----------+-------------+---------------------------------------------------+
 *   | status   | Phase       | percent                                           |
 *   +==========+=============+===================================================+
 *   | DOWNLOAD | downloading | dwl_percent                                       |
 *   | RUN      | installing  | bắt đầu step, cur_percent = 0                     |
 *   | PROGRESS | installing  | ((cur_step - 1) * 100 + cur_percent) / nsteps     |
 *   | còn lại  | bỏ qua      | kết quả cuối lấy từ exit code của swupdate        |
 *   +----------+-------------+---------------------------------------------------+
 *
 *   Đã vào installing thì bỏ qua DOWNLOAD, image installed-directly có thể vừa tải vừa ghi.
 *
 *   SWUpdate gửi với MSG_DONTWAIT, buffer đầy thì nó sleep 1 giây rồi thử lại tối đa 5 lần,
 *   nên đọc chậm sẽ làm chậm cả quá trình cài. Đóng socket thì không sao, SWUpdate có
 *   MSG_NOSIGNAL và chỉ bỏ client đó.
 */

#include "otad/swupdate_progress.h"

#include "common/event_loop.h"
#include "common/log.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if __has_include(<progress_ipc.h>)
#include <progress_ipc.h>
#define SENSORNODE_HAVE_PROGRESS_IPC 1
#endif

namespace sensornode::ota {

namespace {

using namespace std::chrono_literals;

constexpr auto     kRetryPeriod = 100ms;
constexpr unsigned kApiMajor    = 2;

enum Status : std::uint32_t {
    kIdle, kStart, kRun, kSuccess, kFailure, kDownload, kDone, kSubprocess, kProgress,
};

struct ConnectAck {
    std::uint32_t apiversion;
    char          magic[4];
};

struct Msg {
    std::uint32_t      apiversion;
    std::uint32_t      status;
    std::uint32_t      dwl_percent;
    unsigned long long dwl_bytes;
    std::uint32_t      nsteps;
    std::uint32_t      cur_step;
    std::uint32_t      cur_percent;
    char               cur_image[256];
    char               hnd_name[64];
    std::uint32_t      source;
    std::uint32_t      infolen;
    char               info[2048];
};

#ifdef SENSORNODE_HAVE_PROGRESS_IPC
static_assert(sizeof(ConnectAck) == sizeof(progress_connect_ack), "progress_connect_ack layout changed");
static_assert(sizeof(Msg) == sizeof(progress_msg), "progress_msg layout changed");
static_assert(offsetof(Msg, dwl_bytes) == offsetof(progress_msg, dwl_bytes), "progress_msg layout changed");
static_assert(offsetof(Msg, cur_image) == offsetof(progress_msg, cur_image), "progress_msg layout changed");
static_assert(offsetof(Msg, info) == offsetof(progress_msg, info), "progress_msg layout changed");
static_assert(static_cast<int>(kProgress) == static_cast<int>(PROGRESS), "RECOVERY_STATUS changed");
#endif

unsigned api_major(std::uint32_t v) { return (v >> 16) & 0xffff; }

// Chuỗi trong struct không chắc có '\0' ở cuối.
std::string field(const char* s, std::size_t n) {
    return std::string(s, strnlen(s, n));
}

} // namespace

ProgressClient::ProgressClient(EventLoop& loop, std::string path)
    : loop_(loop), path_(std::move(path)), retry_(loop, [this](std::uint64_t) { try_connect(); }) {}

ProgressClient::~ProgressClient() {
    disconnect();
}

void ProgressClient::start() {
    stop();
    installing_ = false;
    last_step_  = 0;
    try_connect();
    if (fd_ < 0) retry_.start_periodic(kRetryPeriod);
}

void ProgressClient::stop() {
    retry_.stop();
    disconnect();
}

void ProgressClient::try_connect() {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path_.size() >= sizeof addr.sun_path) {
        SN_LOG_ERR("swupdate", "progress socket path too long path=%s", path_.c_str());
        retry_.stop();
        return;
    }
    std::memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        SN_LOG_ERR("swupdate", "socket failed: %s", log::errstr(errno).c_str());
        return;
    }

    // AF_UNIX connect không trả EINPROGRESS: hoặc xong ngay, hoặc lỗi (ENOENT, ECONNREFUSED, EAGAIN).
    if (connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
        SN_LOG_DEBUG("swupdate", "progress connect failed: %s, retrying", log::errstr(errno).c_str());
        ::close(fd);
        return;
    }

    retry_.stop();
    fd_    = fd;
    acked_ = false;
    in_.clear();
    loop_.add(fd_, EPOLLIN, [this](std::uint32_t) { on_readable(); });
    SN_LOG_INFO("swupdate", "progress connected path=%s", path_.c_str());
}

void ProgressClient::on_readable() {
    char buf[4096];

    while (fd_ >= 0) {
        const ssize_t n = ::read(fd_, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) return;
            SN_LOG_WARN("swupdate", "progress read failed: %s", log::errstr(errno).c_str());
            disconnect();
            return;
        }
        // swupdate thoát thì socket đóng, kết quả sẽ đến qua exit code.
        if (n == 0) {
            SN_LOG_DEBUG("swupdate", "progress socket closed by swupdate");
            disconnect();
            return;
        }

        in_.append(buf, static_cast<std::size_t>(n));

        if (!acked_ && !consume_ack()) return;

        std::size_t off = 0;
        while (in_.size() - off >= sizeof(Msg)) {
            dispatch(in_.data() + off);
            off += sizeof(Msg);
            if (fd_ < 0) return;
        }
        in_.erase(0, off);
    }
}

bool ProgressClient::consume_ack() {
    if (in_.size() < sizeof(ConnectAck)) return false;

    ConnectAck ack;
    std::memcpy(&ack, in_.data(), sizeof ack);
    if (std::memcmp(ack.magic, "ACK", 4) != 0 || api_major(ack.apiversion) != kApiMajor) {
        SN_LOG_WARN("swupdate", "progress handshake mismatch api=0x%x, progress disabled",
                    static_cast<unsigned>(ack.apiversion));
        disconnect();
        return false;
    }

    in_.erase(0, sizeof ack);
    acked_ = true;
    return true;
}

void ProgressClient::dispatch(const char* raw) {
    // memcpy thay vì reinterpret_cast: dữ liệu trong std::string không đảm bảo căn lề 8.
    Msg m;
    std::memcpy(&m, raw, sizeof m);

    switch (m.status) {
    case kDownload:
        if (installing_) return;
        if (on_progress_) on_progress_(Phase::downloading, static_cast<int>(std::min(m.dwl_percent, 100u)));
        break;

    // PROGRESS còn dùng cho sự kiện info, nhưng cur_step/nsteps vẫn đúng nên tính như nhau.
    case kRun:
    case kProgress: {
        if (m.nsteps == 0 || m.cur_step == 0) return;
        const unsigned step = std::min(m.cur_step, m.nsteps);
        if (step != last_step_) {
            last_step_ = step;
            SN_LOG_INFO("swupdate", "step %u/%u image=%s handler=%s", step, m.nsteps,
                        field(m.cur_image, sizeof m.cur_image).c_str(),
                        field(m.hnd_name, sizeof m.hnd_name).c_str());
        }
        installing_ = true;
        const unsigned pct = ((step - 1) * 100 + std::min(m.cur_percent, 100u)) / m.nsteps;
        if (on_progress_) on_progress_(Phase::installing, static_cast<int>(pct));
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
    in_.clear();
}

} // namespace sensornode::ota
