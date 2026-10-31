#pragma once

#include "common/timer.h"

#include <cstdint>
#include <deque>
#include <string>

namespace sensornode {

class EventLoop;
class MqttClient;
class Storage;

// Gửi mẫu từ DB lên broker theo thứ tự ts, con trỏ chỉ tiến khi có PUBACK nên mất
// broker hay restart daemon đều gửi tiếp được từ chỗ dừng.
class Forwarder {
public:
    Forwarder(EventLoop& loop, Storage& storage, MqttClient& mqtt, std::string topic);

    Forwarder(const Forwarder&) = delete;
    Forwarder& operator=(const Forwarder&) = delete;

    void on_sample(std::int64_t ts_ms);     // mẫu vừa được ghi vào DB
    void on_connect();
    void save();

private:
    struct Inflight {
        int          mid;
        std::int64_t ts_ms;
        bool         acked;
    };

    void pump();
    void on_ack(int mid);

    Storage&    storage_;
    MqttClient& mqtt_;
    std::string topic_;

    std::int64_t acked_ts_;
    std::int64_t sent_ts_;
    std::int64_t latest_ts_ = 0;    // không gửi mẫu mới hơn mẫu vừa đo
    bool         dirty_     = false;

    std::deque<Inflight> inflight_;
    Timer                save_timer_;
};

} // namespace sensornode
