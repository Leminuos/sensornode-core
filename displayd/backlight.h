#pragma once

#include <string>

namespace sensornode::display {

class Backlight {
public:
    Backlight(const std::string& chip_dir, int channel);

    Backlight(const Backlight&) = delete;
    Backlight& operator=(const Backlight&) = delete;

    bool set_brightness(int value);

private:
    std::string channel_dir_;
    bool        enabled_ = false;
};

} // namespace sensornode::display
