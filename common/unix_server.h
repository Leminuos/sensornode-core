#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sensornode {

class EventLoop;

struct PeerCred {
    pid_t pid;
    uid_t uid;
    gid_t gid;
};

class UnixServer {
public:
    using ClientId = std::uint64_t;

    struct Options {
        std::string path;
        mode_t      mode            = 0660;
        std::size_t max_clients     = 16;
        std::size_t max_line        = 4096;
        std::size_t max_pending_out = 64 * 1024;
    };

    using ConnectHandler    = std::function<bool(ClientId, const PeerCred&)>;
    using LineHandler       = std::function<void(ClientId, const std::string& line)>;
    using DisconnectHandler = std::function<void(ClientId, const char* reason)>;

    UnixServer(EventLoop& loop, Options opts);
    ~UnixServer();

    UnixServer(const UnixServer&) = delete;
    UnixServer& operator=(const UnixServer&) = delete;

    void on_connect(ConnectHandler h)       { on_connect_ = std::move(h); }
    void on_line(LineHandler h)             { on_line_ = std::move(h); }
    void on_disconnect(DisconnectHandler h) { on_disconnect_ = std::move(h); }

    bool send(ClientId id, std::string_view line);
    void broadcast(std::string_view line);
    void close(ClientId id, const char* reason);

    const PeerCred* peer(ClientId id) const;
    std::size_t client_count() const noexcept { return clients_.size(); }

private:
    struct Client {
        int           fd;
        PeerCred      cred;
        std::string   in;                   // dữ liệu đã nhận, chưa đủ một dòng
        std::string   out;                  // dữ liệu chờ gửi
        std::uint32_t events      = 0;      // sự kiện đang đăng ký với epoll
        bool          peer_closed = false;  // client đã shutdown chiều gửi
    };

    void open_listener();
    void accept_clients();
    void add_client(int fd);
    void on_client_ready(ClientId id, std::uint32_t revents);
    bool read_from(ClientId id);
    bool dispatch_lines(ClientId id);
    bool write_pending(ClientId id);
    bool finish_io(ClientId id);
    Client* find(ClientId id);

    EventLoop& loop_;
    Options    opts_;
    int        listen_fd_ = -1;
    ino_t      sock_ino_  = 0;
    ClientId   next_id_   = 1;
    std::unordered_map<ClientId, std::unique_ptr<Client>> clients_;

    ConnectHandler    on_connect_;
    LineHandler       on_line_;
    DisconnectHandler on_disconnect_;
};

} // namespace sensornode
