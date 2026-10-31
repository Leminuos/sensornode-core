#include "telemetryd/forwarder.h"

#include "common/log.h"
#include "common/mqtt_client.h"
#include "telemetryd/protocol.h"
#include "telemetryd/storage.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstddef>

namespace sensornode {

namespace {

using namespace std::chrono_literals;

constexpr std::size_t kWindow   = 20;
constexpr std::size_t kLowWater = kWindow / 2;

constexpr auto kSavePeriod = 30s;

} // namespace

Forwarder::Forwarder(EventLoop& loop, Storage& storage, MqttClient& mqtt, std::string topic)
    : storage_(storage),
      mqtt_(mqtt),
      topic_(std::move(topic)),
      acked_ts_(storage.forward_cursor()),
      sent_ts_(acked_ts_),
      save_timer_(loop, [this](std::uint64_t) { save(); }) {
    mqtt_.on_publish([this](int mid) { on_ack(mid); });
    save_timer_.start_periodic(kSavePeriod);
}

void Forwarder::on_sample(std::int64_t ts_ms) {
    if (ts_ms <= sent_ts_) {
        SN_LOG_WARN("forward", "clock moved back cursor=%" PRId64 " ts=%" PRId64, sent_ts_, ts_ms);
        inflight_.clear();
        sent_ts_  = ts_ms - 1;
        acked_ts_ = sent_ts_;
        dirty_    = true;
    }

    latest_ts_ = ts_ms;
    pump();
}

void Forwarder::on_connect() {
    pump();
}

void Forwarder::save() {
    if (!dirty_) return;
    if (storage_.save_forward_cursor(acked_ts_)) {
        dirty_ = false;
    } else {
        SN_LOG_ERR("forward", "save cursor failed: %s", storage_.errmsg());
    }
}

void Forwarder::pump() {
    if (!mqtt_.connected() || inflight_.size() > kLowWater) return;

    const int room = static_cast<int>(kWindow - inflight_.size());
    for (const Sample& s : storage_.pending(sent_ts_, latest_ts_, room)) {
        int mid = 0;
        if (!mqtt_.publish(topic_, make_sample_payload(s), 1, false, &mid)) break;
        inflight_.push_back({mid, s.ts_ms, false});
        sent_ts_ = s.ts_ms;
    }
}

void Forwarder::on_ack(int mid) {
    const auto it = std::find_if(inflight_.begin(), inflight_.end(),[mid](const Inflight& m) { 
        return m.mid == mid; 
    });

    if (it == inflight_.end()) return;
    it->acked = true;

    while (!inflight_.empty() && inflight_.front().acked) {
        acked_ts_ = inflight_.front().ts_ms;
        dirty_    = true;
        inflight_.pop_front();
    }
    
    pump();
}

} // namespace sensornode
