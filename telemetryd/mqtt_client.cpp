#include "telemetryd/mqtt_client.h"
#include "common/event_loop.h"

#include <mosquitto.h>
#include <sys/epoll.h>

#include <algorithm>
#include <stdexcept>

namespace sensornode {

namespace {

constexpr std::chrono::seconds kMiscPeriod{1};

struct LibMosquitto {
    LibMosquitto()  { mosquitto_lib_init(); }
    ~LibMosquitto() { mosquitto_lib_cleanup(); }
};

void ensure_lib_init() {
    static LibMosquitto lib;
}

} // namespace

// Tạo handle mosquitto và đăng ký callback
MqttClient::MqttClient(EventLoop& loop, Options opts)
    : loop_(loop),
      opts_(std::move(opts)),
      backoff_(opts_.min_backoff),
      misc_timer_(loop, [this](std::uint64_t) { on_misc_tick(); }),
      reconnect_timer_(loop, [this](std::uint64_t) { try_connect(); }) {

    ensure_lib_init();

    const char* id = opts_.client_id.empty() ? nullptr : opts_.client_id.c_str();
    mosq_ = mosquitto_new(id, true, this);
    if (!mosq_) throw std::runtime_error("mosquitto_new failed");

    mosquitto_connect_callback_set(mosq_, &MqttClient::handle_connect);
    mosquitto_disconnect_callback_set(mosq_, &MqttClient::handle_disconnect);

    // Đăng ký Last Will với thư viện, gửi kèm packet CONNECT ở mỗi lần kết nối.
    if (!opts_.will_topic.empty()) {
        const int rc = mosquitto_will_set(mosq_, opts_.will_topic.c_str(),
                                          static_cast<int>(opts_.will_payload.size()),
                                          opts_.will_payload.data(), 1, true);
        if (rc != MOSQ_ERR_SUCCESS) {
            mosquitto_destroy(mosq_);
            throw std::runtime_error(std::string("mosquitto_will_set: ") + mosquitto_strerror(rc));
        }
    }
}

// Gỡ socket khỏi epoll trước khi mosquitto_destroy() close nó.
MqttClient::~MqttClient() {
    stop();
    detach_socket();
    mosquitto_destroy(mosq_);
}

// Bật timer keepalive và kết nối lần đầu
void MqttClient::start() {
    if (started_) return;
    started_ = true;
    misc_timer_.start_periodic(kMiscPeriod);
    try_connect();
}

// Ngừng reconnect và ngắt kết nối
void MqttClient::stop() {
    if (!started_ || stopping_) return;
    stopping_ = true;
    reconnect_timer_.stop();
    misc_timer_.stop();

    if (connected_) {
        // Tự publish will vì broker bỏ will khi client gửi DISCONNECT.
        if (!opts_.will_topic.empty()) {
            publish(opts_.will_topic, opts_.will_payload, 1, true);
        }

        mosquitto_disconnect(mosq_);
    }
    detach_socket();
    connected_ = false;
}

// Gửi một message
bool MqttClient::publish(const std::string& topic, std::string_view payload, int qos, bool retain) {
    if (!connected_) return false;

    const int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(),
                                     static_cast<int>(payload.size()),
                                     payload.data(), qos, retain);

    update_events();
    return rc == MOSQ_ERR_SUCCESS;
}

// Mở kết nối TCP non-blocking, CONNECT được gửi khi socket ghi được.
void MqttClient::try_connect() {
    if (stopping_) return;

    const int rc = host_set_
        ? mosquitto_reconnect_async(mosq_)
        : mosquitto_connect_async(mosq_, opts_.host.c_str(), opts_.port, opts_.keepalive);

    if (rc == MOSQ_ERR_INVAL) {
        throw std::invalid_argument("MqttClient: invalid host/port/keepalive");
    }
    host_set_ = true;

    if (rc != MOSQ_ERR_SUCCESS) {
        schedule_reconnect(mosquitto_strerror(rc));
        return;
    }
    attach_socket();
}

// Đăng ký socket mới của thư viện với event loop.
void MqttClient::attach_socket() {
    fd_ = mosquitto_socket(mosq_);
    if (fd_ < 0) return;

    events_ = EPOLLIN | (mosquitto_want_write(mosq_) ? EPOLLOUT : 0u);
    loop_.add(fd_, events_, [this](std::uint32_t ev) { on_socket_ready(ev); });
}

// Gỡ socket khỏi event loop, kernel đã tự gỡ nếu socket đã bị close.
void MqttClient::detach_socket() {
    if (fd_ < 0) return;
    loop_.remove(fd_);
    fd_     = -1;
    events_ = 0;
}

// Cập nhật epoll sau mỗi lần gọi vào thư viện vì hàng đợi gửi có thể đã thay đổi.
void MqttClient::update_events() {
    if (fd_ < 0) return;

    // Luôn chờ đọc, chỉ chờ ghi khi thư viện còn packet chưa gửi hết
    const std::uint32_t want = EPOLLIN | (mosquitto_want_write(mosq_) ? EPOLLOUT : 0u);
    if (want != events_) {
        loop_.modify(fd_, want);
        events_ = want;
    }
}

// Chuyển sự kiện epoll cho thư viện, lỗi được báo qua disconnect_callback.
void MqttClient::on_socket_ready(std::uint32_t revents) {
    if (revents & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
        mosquitto_loop_read(mosq_, 1);
    }
    if (fd_ >= 0 && (revents & EPOLLOUT)) {
        mosquitto_loop_write(mosq_, 1);
    }
    update_events();
}

void MqttClient::on_misc_tick() {
    if (fd_ < 0) return;
    mosquitto_loop_misc(mosq_);
    update_events();
}

void MqttClient::schedule_reconnect(const char* reason) {
    const auto delay = backoff_;
    backoff_ = std::min(backoff_ * 2, opts_.max_backoff);
    reconnect_timer_.start_oneshot(delay);
    if (on_disconnect_) on_disconnect_(reason, delay);
}

void MqttClient::handle_connect(mosquitto*, void* self, int rc) {
    auto* c = static_cast<MqttClient*>(self);

    if (rc != 0) {
        c->connack_ = rc;
        return;
    }

    c->connected_ = true;
    c->backoff_   = c->opts_.min_backoff;
    if (c->on_connect_) c->on_connect_();
}

void MqttClient::handle_disconnect(mosquitto*, void* self, int rc) {
    auto* c = static_cast<MqttClient*>(self);

    const char* reason = c->connack_ != 0 ? mosquitto_connack_string(c->connack_)
                       : rc == MOSQ_ERR_SUCCESS ? "disconnected"
                       : mosquitto_strerror(rc);
    c->connack_   = 0;
    c->connected_ = false;
    c->detach_socket();

    if (c->stopping_) return;
    c->schedule_reconnect(reason);
}

} // namespace sensornode
