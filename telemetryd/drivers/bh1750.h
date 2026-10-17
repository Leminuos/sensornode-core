#pragma once

#include "telemetryd/drivers/i2c_dev.h"

#include <cstdint>

namespace sensornode {

class Bh1750 {
public:
    // 0x23 khi chân ADDR nối GND, 0x5C khi nối VCC.
    static constexpr std::uint16_t kDefaultAddress = 0x23;

    explicit Bh1750(I2cBus& bus, std::uint16_t address = kDefaultAddress);

    int start();
    int fetch(double& lux);

    static double decode(const std::uint8_t frame[2]);

private:
    I2cBus&       bus_;
    std::uint16_t address_;
};

} // namespace sensornode
