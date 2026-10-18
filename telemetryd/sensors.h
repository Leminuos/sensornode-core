#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sensornode {

constexpr std::chrono::milliseconds kMeasureTime{180};

constexpr const char* kSht30  = "sht30";
constexpr const char* kBh1750 = "bh1750";

struct SensorError {
    const char* sensor;
    int         err;
};

struct Sample {
    std::int64_t ts_ms = 0;     // unix ms
    double temp_c  = 0;
    double humi_rh = 0;
    double lux     = 0;
    std::vector<SensorError> errors;

    bool failed(const char* sensor) const;
};

class Sensors {
public:
    virtual ~Sensors() = default;

    virtual void start() = 0;

    virtual void fetch(Sample& out) = 0;
};

std::unique_ptr<Sensors> make_i2c_sensors(const std::string& bus_path);

std::unique_ptr<Sensors> make_fake_sensors();

} // namespace sensornode
