#include "telemetryd/drivers/bh1750.h"

namespace sensornode {

Bh1750::Bh1750(I2cBus& bus, std::uint16_t address)
    : bus_(bus), address_(address) {}

int Bh1750::start() {
    const std::uint8_t kCmdMeasure[] = {0x20};
    start_err_ = bus_.write(address_, kCmdMeasure, sizeof kCmdMeasure);
    return start_err_;
}

int Bh1750::fetch(double& lux) {
    if (start_err_ != 0) return start_err_;

    std::uint8_t frame[2];

    const int err = bus_.read(address_, frame, sizeof frame);
    if (err != 0) return err;

    lux = decode(frame);
    return 0;
}

double Bh1750::decode(const std::uint8_t frame[2]) {
    const unsigned raw = (frame[0] << 8) | frame[1];
    return raw / 1.2;
}

} // namespace sensornode
