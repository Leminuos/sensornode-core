#include "telemetryd/sensors.h"

#include "telemetryd/drivers/bh1750.h"
#include "telemetryd/drivers/i2c_dev.h"
#include "telemetryd/drivers/sht30.h"

#include "common/log.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace sensornode {

bool Sample::failed(const char* sensor) const {
    for (const auto& e : errors) {
        if (std::strcmp(e.sensor, sensor) == 0) return true;
    }
    return false;
}

class I2cSensors final : public Sensors {
public:
    explicit I2cSensors(const std::string& bus_path)
        : bus_(bus_path), sht30_(bus_), bh1750_(bus_) {}

    void start() override {
        sht30_.start();
        bh1750_.start();
    }

    void fetch(Sample& out) override {
        Sht30::Reading r;
        int err = sht30_.fetch(r);
        if (err == 0) {
            out.temp_c  = r.temp_c;
            out.humi_rh = r.humi_rh;
        }
        record(kSht30, err, sht30_last_err_, out);

        double lux = 0;
        err = bh1750_.fetch(lux);
        if (err == 0) out.lux = lux;
        record(kBh1750, err, bh1750_last_err_, out);
    }

private:
    // Ghi lỗi vào sample và chỉ log khi lỗi đổi để sensor hỏng lâu không làm ngập log.
    static void record(const char* sensor, int err, int& last_err, Sample& out) {
        if (err != 0) out.errors.push_back({sensor, err});
        if (err == last_err) return;

        if (err != 0) {
            SN_LOG_WARN("sensor", "read failed: %s sensor=%s", log::errstr(-err).c_str(), sensor);
        } else {
            SN_LOG_NOTICE("sensor", "recovered sensor=%s", sensor);
        }
        last_err = err;
    }

    I2cBus bus_;
    Sht30  sht30_;
    Bh1750 bh1750_;
    int    sht30_last_err_  = 0;
    int    bh1750_last_err_ = 0;
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
    auto sensors = std::make_unique<I2cSensors>(bus_path);
    SN_LOG_INFO("sensor", "using i2c bus=%s sht30=0x%02x bh1750=0x%02x", bus_path.c_str(),
                Sht30::kDefaultAddress, Bh1750::kDefaultAddress);
    return sensors;
}

std::unique_ptr<Sensors> make_fake_sensors() {
    // Mức warning để bản build fake lỡ lên board thật vẫn nổi bật khi lọc log.
    SN_LOG_WARN("sensor", "using fake sensors");
    return std::make_unique<FakeSensors>();
}

} // namespace sensornode
