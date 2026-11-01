#pragma once

#include <string>

namespace sensornode::display {

class Backlight {
public:
    explicit Backlight(const std::string& device_dir);

    Backlight(const Backlight&) = delete;
    Backlight& operator=(const Backlight&) = delete;

    bool set_brightness(int value);

private:
    std::string brightness_path_;
};

} // namespace sensornode::display
