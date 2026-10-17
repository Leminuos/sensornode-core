#include "telemetryd/drivers/sht30.h"

#include <cerrno>

namespace sensornode {

Sht30::Sht30(I2cBus& bus, std::uint16_t address)
    : bus_(bus), address_(address) {}

int Sht30::start() {
    const std::uint8_t kCmdMeasure[] = {0x24, 0x00};
    return bus_.write(address_, kCmdMeasure, sizeof kCmdMeasure);
}

int Sht30::fetch(Reading& out) {
    std::uint8_t frame[6];

    const int err = bus_.read(address_, frame, sizeof frame);
    if (err != 0) return err;

    return decode(frame, out);
}

std::uint8_t Sht30::crc8(const std::uint8_t* data, std::size_t len) {
    std::uint8_t crc = 0xFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = static_cast<std::uint8_t>((crc << 1) ^ 0x31);
            } else {
                crc = static_cast<std::uint8_t>(crc << 1);
            }
        }
    }
    return crc;
}

int Sht30::decode(const std::uint8_t frame[6], Reading& out) {
    if (crc8(&frame[0], 2) != frame[2]) return -EBADMSG;
    if (crc8(&frame[3], 2) != frame[5]) return -EBADMSG;

    const unsigned raw_t  = (frame[0] << 8) | frame[1];
    const unsigned raw_rh = (frame[3] << 8) | frame[4];

    out.temp_c  = -45.0 + 175.0 * raw_t / 65535.0;
    out.humi_rh = 100.0 * raw_rh / 65535.0;
    return 0;
}

} // namespace sensornode
