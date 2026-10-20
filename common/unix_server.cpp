#include "common/unix_server.h"
#include "common/event_loop.h"
#include "common/log.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace sensornode {

namespace {

constexpr int kBacklog = 16;

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

// Tạo địa chỉ socket từ path, throw nếu path rỗng hoặc dài quá sun_path.
sockaddr_un make_addr(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof addr.sun_path) {
        throw std::invalid_argument("UnixServer: invalid socket path '" + path + "'");
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return addr;
}

// Inode của file tại path, 0 nếu file không tồn tại.
ino_t inode_of(const std::string& path) {
    struct stat st{};
    return lstat(path.c_str(), &st) == 0 ? st.st_ino : 0;
}

// Thử connect để biết có process nào đang listen trên path không.
bool socket_in_use(const std::string& path) {
    const int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (probe < 0) throw_errno("socket");

    const sockaddr_un addr = make_addr(path);
    const int r = connect(probe, reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    const int err = errno;
    ::close(probe);

    if (r == 0 || err == EAGAIN) return true;
    if (err == ECONNREFUSED) return false;
    throw std::system_error(err, std::generic_category(), "connect(" + path + ")");
}

// Xoá socket file còn sót từ lần chạy trước, throw nếu file đang được dùng hoặc không phải socket.
void remove_stale_socket(const std::string& path) {
    struct stat st{};
    if (lstat(path.c_str(), &st) < 0) {
        if (errno == ENOENT) return;
        throw_errno("lstat(" + path + ")");
    }
    if (!S_ISSOCK(st.st_mode)) {
        throw std::runtime_error(path + " exists and is not a socket");
    }
    if (socket_in_use(path)) {
        throw std::runtime_error(path + ": another instance is already listening");
    }
    if (unlink(path.c_str()) < 0) throw_errno("unlink(" + path + ")");
}

// bind() sao cho socket file sinh ra có đúng quyền mode.
void bind_with_mode(int fd, const sockaddr_un& addr, mode_t mode) {
    const mode_t old_mask = umask(~mode & 0777);
    const int r = bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    const int err = errno;
    umask(old_mask);
    if (r < 0) {
        throw std::system_error(err, std::generic_category(),
                                std::string("bind(") + addr.sun_path + ")");
    }
}

// Báo lỗi cho client bị từ chối trước khi đóng, không chờ nếu client không đọc.
void reject_client(int fd, const PeerCred& cred, const char* reason) {
    const std::string msg = std::string("{\"type\":\"error\",\"msg\":\"") + reason + "\"}\n";
    (void)::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    ::close(fd);
    SN_LOG_WARN("ipc", "client rejected: %s pid=%d uid=%d gid=%d", reason,
                static_cast<int>(cred.pid), static_cast<int>(cred.uid), static_cast<int>(cred.gid));
}

} // namespace

UnixServer::UnixServer(EventLoop& loop, Options opts)
    : loop_(loop), opts_(std::move(opts)) {
    open_listener();
}

UnixServer::~UnixServer() {
    for (const auto& kv : clients_) {
        loop_.remove(kv.second->fd);
        ::close(kv.second->fd);
    }
    clients_.clear();

    loop_.remove(listen_fd_);
    ::close(listen_fd_);

    if (inode_of(opts_.path) == sock_ino_) unlink(opts_.path.c_str());
}

// Tạo socket listen và đăng ký với event loop.
void UnixServer::open_listener() {
    const sockaddr_un addr = make_addr(opts_.path);
    remove_stale_socket(opts_.path);

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) throw_errno("socket");

    try {
        bind_with_mode(listen_fd_, addr, opts_.mode);
        sock_ino_ = inode_of(opts_.path);

        if (listen(listen_fd_, kBacklog) < 0) throw_errno("listen");
        loop_.add(listen_fd_, EPOLLIN, [this](std::uint32_t) { accept_clients(); });
    } catch (...) {
        ::close(listen_fd_);
        if (sock_ino_ != 0) unlink(opts_.path.c_str());
        throw;
    }
}

// Nhận hết các kết nối đang chờ trong backlog.
void UnixServer::accept_clients() {
    for (;;) {
        const int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
            add_client(fd);
            continue;
        }
        if (errno == EINTR || errno == ECONNABORTED) continue;
        if (errno != EAGAIN) SN_LOG_ERR("ipc", "accept failed: %s", log::errstr(errno).c_str());
        return;
    }
}

// Lấy peer cred, kiểm tra giới hạn, đăng ký client với event loop và báo on_connect.
void UnixServer::add_client(int fd) {
    ucred cr{};
    socklen_t len = sizeof cr;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &len) < 0) {
        SN_LOG_ERR("ipc", "peer credentials failed: %s", log::errstr(errno).c_str());
        ::close(fd);
        return;
    }
    const PeerCred cred{cr.pid, cr.uid, cr.gid};

    if (clients_.size() >= opts_.max_clients) {
        reject_client(fd, cred, "too many clients");
        return;
    }

    const ClientId id = next_id_++;
    try {
        loop_.add(fd, EPOLLIN, [this, id](std::uint32_t ev) { on_client_ready(id, ev); });
    } catch (...) {
        ::close(fd);
        throw;
    }
    clients_.emplace(id, std::make_unique<Client>(Client{fd, cred, {}, {}, EPOLLIN, false}));

    if (on_connect_ && !on_connect_(id, cred)) {
        close(id, "rejected by policy");
    }
}

// Xử lý sự kiện epoll của một client.
void UnixServer::on_client_ready(ClientId id, std::uint32_t revents) {
    if (revents & EPOLLERR) {
        close(id, "socket error");
        return;
    }
    if (revents & (EPOLLIN | EPOLLHUP)) {
        if (!read_from(id)) return;
    }
    if (revents & (EPOLLOUT | EPOLLHUP)) {
        write_pending(id);
    }
}

// Đọc tới khi socket hết dữ liệu, chuyển từng dòng hoàn chỉnh cho on_line.
bool UnixServer::read_from(ClientId id) {
    char buf[4096];
    for (;;) {
        Client* c = find(id);
        if (!c) return false;

        const ssize_t n = recv(c->fd, buf, sizeof buf, 0);
        if (n > 0) {
            c->in.append(buf, static_cast<std::size_t>(n));
            if (!dispatch_lines(id)) return false;
            continue;
        }
        if (n == 0) {
            c->peer_closed = true;
            break;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN) break;
        close(id, std::strerror(errno));
        return false;
    }
    return finish_io(id);
}

// Tách các dòng hoàn chỉnh khỏi buffer nhận và gọi on_line
bool UnixServer::dispatch_lines(ClientId id) {
    for (Client* c = find(id); c != nullptr; c = find(id)) {
        const std::size_t nl = c->in.find('\n');
        if (nl == std::string::npos) {
            if (c->in.size() <= opts_.max_line) return true;
            close(id, "line too long");
            return false;
        }

        std::string line = c->in.substr(0, nl);
        c->in.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.size() > opts_.max_line) {
            close(id, "line too long");
            return false;
        }
        if (!line.empty() && on_line_) on_line_(id, line);
    }
    return false;
}

// Ghi buffer pending gửi xuống socket tới khi hết hoặc socket đầy.
bool UnixServer::write_pending(ClientId id) {
    Client* c = find(id);
    if (!c) return false;

    while (!c->out.empty()) {
        const ssize_t n = ::send(c->fd, c->out.data(), c->out.size(), MSG_NOSIGNAL);
        if (n > 0) {
            c->out.erase(0, static_cast<std::size_t>(n));
            continue;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN) break;
        close(id, std::strerror(errno));
        return false;
    }
    
    return finish_io(id);
}

bool UnixServer::finish_io(ClientId id) {
    Client* c = find(id);
    if (!c) return false;

    if (c->peer_closed && c->out.empty()) {
        close(id, "peer closed");
        return false;
    }

    std::uint32_t want = 0;
    if (!c->peer_closed)  want |= EPOLLIN;
    if (!c->out.empty())  want |= EPOLLOUT;

    if (want != c->events) {
        loop_.modify(c->fd, want);
        c->events = want;
    }
    return true;
}

bool UnixServer::send(ClientId id, std::string_view line) {
    Client* c = find(id);
    if (!c) return false;

    if (c->out.size() + line.size() + 1 > opts_.max_pending_out) {
        close(id, "output buffer full (slow client)");
        return false;
    }

    const bool idle = c->out.empty();
    c->out.append(line.data(), line.size());
    c->out.push_back('\n');

    return idle ? write_pending(id) : true;
}

void UnixServer::broadcast(std::string_view line) {
    std::vector<ClientId> ids;
    ids.reserve(clients_.size());
    for (const auto& kv : clients_) ids.push_back(kv.first);

    for (const ClientId id : ids) send(id, line);
}

void UnixServer::close(ClientId id, const char* reason) {
    const auto it = clients_.find(id);
    if (it == clients_.end()) return;

    loop_.remove(it->second->fd);
    ::close(it->second->fd);
    clients_.erase(it);

    if (on_disconnect_) on_disconnect_(id, reason);
}

const PeerCred* UnixServer::peer(ClientId id) const {
    const auto it = clients_.find(id);
    return it == clients_.end() ? nullptr : &it->second->cred;
}

// Con trỏ tới client, nullptr nếu đã bị đóng.
UnixServer::Client* UnixServer::find(ClientId id) {
    const auto it = clients_.find(id);
    return it == clients_.end() ? nullptr : it->second.get();
}

} // namespace sensornode
