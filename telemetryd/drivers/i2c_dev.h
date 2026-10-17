#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sensornode {

class I2cBus {
public:
    explicit I2cBus(const std::string& path);
    ~I2cBus();

    I2cBus(const I2cBus&) = delete;
    I2cBus& operator=(const I2cBus&) = delete;

    int write(std::uint16_t addr, const std::uint8_t* data, std::size_t len);
    int read(std::uint16_t addr, std::uint8_t* buf, std::size_t len);

private:
    int fd_ = -1;

    int transfer(std::uint16_t addr, std::uint16_t flags, std::uint8_t* buf, std::size_t len);
};

} // namespace sensornode
