#include "telemetryd/sensors.h"

#include "telemetryd/drivers/bh1750.h"
#include "telemetryd/drivers/i2c_dev.h"
#include "telemetryd/drivers/sht30.h"

#include <cmath>
#include <cstdint>

namespace sensornode {

class I2cSensors final : public Sensors {
public:
    explicit I2cSensors(const std::string& bus_path)
        : bus_(bus_path), sht30_(bus_), bh1750_(bus_) {}

    void start() override {
        sht30_start_err_  = sht30_.start();
        bh1750_start_err_ = bh1750_.start();
    }

    void fetch(Sample& out) override {
        if (sht30_start_err_ != 0) {
            out.errors.push_back({"sht30", sht30_start_err_});
        } else {
            Sht30::Reading r;
            const int err = sht30_.fetch(r);
            if (err == 0) {
                out.temp_c  = r.temp_c;
                out.humi_rh = r.humi_rh;
            } else {
                out.errors.push_back({"sht30", err});
            }
        }

        if (bh1750_start_err_ != 0) {
            out.errors.push_back({"bh1750", bh1750_start_err_});
        } else {
            double lux = 0;
            const int err = bh1750_.fetch(lux);
            if (err == 0) {
                out.lux = lux;
            } else {
                out.errors.push_back({"bh1750", err});
            }
        }
    }

private:
    I2cBus bus_;
    Sht30  sht30_;
    Bh1750 bh1750_;
    int    sht30_start_err_  = 0;
    int    bh1750_start_err_ = 0;
};

class FakeSensors final : public Sensors {
public:
    void start() override {}

    void fetch(Sample& out) override {
        constexpr double kPi = 3.14159265358979323846;
        const double phase = std::sin(2.0 * kPi * static_cast<double>(count_) / 120.0);
        ++count_;

        out.temp_c  = 24.0 + 3.0 * phase;
        out.humi_rh = 55.0 - 10.0 * phase;
        out.lux     = 300.0 + 200.0 * phase;
    }

private:
    std::uint64_t count_ = 0;
};

std::unique_ptr<Sensors> make_i2c_sensors(const std::string& bus_path) {
    return std::make_unique<I2cSensors>(bus_path);
}

std::unique_ptr<Sensors> make_fake_sensors() {
    return std::make_unique<FakeSensors>();
}

} // namespace sensornode
