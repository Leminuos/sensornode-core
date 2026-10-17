#pragma once

#include "telemetryd/drivers/i2c_dev.h"

#include <cstddef>
#include <cstdint>

namespace sensornode {

class Sht30 {
public:
    // 0x44 khi chân ADDR nối GND, 0x45 khi nối VDD.
    static constexpr std::uint16_t kDefaultAddress = 0x44;

    struct Reading {
        double temp_c  = 0;
        double humi_rh = 0;
    };

    explicit Sht30(I2cBus& bus, std::uint16_t address = kDefaultAddress);

    int start();
    int fetch(Reading& out);

    static std::uint8_t crc8(const std::uint8_t* data, std::size_t len);
    static int decode(const std::uint8_t frame[6], Reading& out);

private:
    I2cBus&       bus_;
    std::uint16_t address_;
};

} // namespace sensornode
