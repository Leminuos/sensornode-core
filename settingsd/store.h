#pragma once

#include "common/timer.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sensornode {
class EventLoop;
}

namespace sensornode::settings {

// Nguồn duy nhất của thông số người dùng đổi được. Khai báo key trong kSchema (store.cpp).
class Store {
public:
    Store(EventLoop& loop, std::string path);

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    const nlohmann::json& values() const noexcept { return values_; }

    // Trả về nullptr nếu thành công, ngược lại là lý do từ chối.
    const char* set(const std::string& key, const nlohmann::json& value);

    void flush();

private:
    void load();
    bool save();

    std::string    path_;
    nlohmann::json root_   = nlohmann::json::object();  // giữ cả key không biết
    nlohmann::json values_ = nlohmann::json::object();
    bool           dirty_  = false;
    Timer          save_timer_;
};

} // namespace sensornode::settings
