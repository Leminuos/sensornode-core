#include "displayd/backlight.h"

#include "common/log.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <stdexcept>

namespace sensornode::display {

namespace {

constexpr std::uint64_t kPeriodNs = 1000000;

// Slider 0..100 -> duty %, theo CIE: Y = ((L+16)/116)^3 nếu L>8, ngược lại Y = L/903.3.
constexpr int kBrightnessLut[101] = {
     10,  10,  10,  10,  10,  10,  11,  11,  11,  11,  // 0..9
     11,  11,  11,  11,  12,  12,  12,  12,  12,  12,  // 10..19
     13,  13,  13,  13,  14,  14,  14,  15,  15,  15,  // 20..29
     16,  16,  16,  17,  17,  18,  18,  19,  19,  20,  // 30..39
     20,  21,  21,  22,  22,  23,  24,  24,  25,  26,  // 40..49
     27,  27,  28,  29,  30,  31,  32,  32,  33,  34,  // 50..59
     35,  36,  37,  38,  40,  41,  42,  43,  44,  45,  // 60..69
     47,  48,  49,  51,  52,  53,  55,  56,  58,  59,  // 70..79
     61,  63,  64,  66,  68,  69,  71,  73,  75,  77,  // 80..89
     79,  81,  83,  85,  87,  89,  91,  93,  95,  98,  // 90..99
    100,                                               // 100
};

bool write_attr(const std::string& path, const std::string& value) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        SN_LOG_ERR("backlight", "open failed: %s path=%s", log::errstr(errno).c_str(), path.c_str());
        return false;
    }

    const bool ok = ::write(fd, value.data(), value.size()) == static_cast<ssize_t>(value.size());
    if (!ok) {
        SN_LOG_ERR("backlight", "write failed: %s path=%s value=%s",
                   log::errstr(errno).c_str(), path.c_str(), value.c_str());
    }
    ::close(fd);
    return ok;
}

bool exists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

} // namespace

Backlight::Backlight(const std::string& chip_dir, int channel)
    : channel_dir_(chip_dir + "/pwm" + std::to_string(channel)) {
    // Kernel tạo pwmN ngay trong lúc ghi export, không phải chờ udev vì daemon chạy root.
    if (!exists(channel_dir_) && !write_attr(chip_dir + "/export", std::to_string(channel))) {
        throw std::runtime_error("cannot export PWM channel " + channel_dir_);
    }

    // Kernel từ chối duty_cycle khi period còn bằng 0 nên phải ghi period trước.
    if (!write_attr(channel_dir_ + "/period", std::to_string(kPeriodNs))) {
        throw std::runtime_error("cannot configure " + channel_dir_);
    }
}

bool Backlight::set_brightness(int value) {
    const std::uint64_t percent = kBrightnessLut[std::clamp(value, 0, 100)];
    if (!write_attr(channel_dir_ + "/duty_cycle", std::to_string(kPeriodNs * percent / 100))) return false;

    if (!enabled_) enabled_ = write_attr(channel_dir_ + "/enable", "1");
    return enabled_;
}

} // namespace sensornode::display
