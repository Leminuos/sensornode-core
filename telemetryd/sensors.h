#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace sensornode {

constexpr std::chrono::milliseconds kMeasureTime{180};

struct SensorError {
    const char* sensor;
    int         err;
};

struct Sample {
    double temp_c  = 0;
    double humi_rh = 0;
    double lux     = 0;
    std::vector<SensorError> errors;
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
