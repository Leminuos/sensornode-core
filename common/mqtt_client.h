#pragma once

#include "common/timer.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct mosquitto;
struct mosquitto_message;

namespace sensornode {

class EventLoop;

class MqttClient {
public:
    struct Options {
        std::string host;
        int         port      = 1883;
        std::string client_id;
        int         keepalive = 30;     // giây

        // Last Will (QoS 1, retained)
        std::string will_topic;
        std::string will_payload;

        std::chrono::seconds min_backoff{1};
        std::chrono::seconds max_backoff{60};
    };

    using ConnectHandler    = std::function<void()>;
    using DisconnectHandler = std::function<void(const char* reason, std::chrono::seconds retry_in)>;
    using MessageHandler    = std::function<void(const std::string& topic, std::string_view payload,
                                                 bool retained)>;

    MqttClient(EventLoop& loop, Options opts);
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    void on_connect(ConnectHandler h)       { on_connect_ = std::move(h); }
    void on_disconnect(DisconnectHandler h) { on_disconnect_ = std::move(h); }
    void on_message(MessageHandler h)       { on_message_ = std::move(h); }

    void start();
    void stop();

    bool publish(const std::string& topic, std::string_view payload, int qos, bool retain);

    // Gọi trước start(), được subscribe lại sau mỗi lần kết nối vì session không được giữ.
    void subscribe(std::string topic, int qos);

    bool connected() const noexcept { return connected_; }

private:
    static void handle_connect(mosquitto*, void* self, int rc);
    static void handle_disconnect(mosquitto*, void* self, int rc);
    static void handle_message(mosquitto*, void* self, const mosquitto_message* msg);

    void try_connect();
    void attach_socket();
    void detach_socket();
    void update_events();
    void on_socket_ready(std::uint32_t revents);
    void on_misc_tick();
    void schedule_reconnect(const char* reason);

    EventLoop& loop_;
    Options    opts_;
    mosquitto* mosq_ = nullptr;

    int           fd_     = -1;
    std::uint32_t events_ = 0;

    bool started_   = false;
    bool stopping_  = false;
    bool connected_ = false;
    bool host_set_  = false;
    int  connack_   = 0;
    std::chrono::seconds backoff_;

    Timer misc_timer_;
    Timer reconnect_timer_;

    std::vector<std::pair<std::string, int>> subscriptions_;

    ConnectHandler    on_connect_;
    DisconnectHandler on_disconnect_;
    MessageHandler    on_message_;
};

} // namespace sensornode
