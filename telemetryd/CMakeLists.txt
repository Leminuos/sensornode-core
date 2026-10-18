add_library(telemetry_core STATIC
    protocol.cpp
    sensors.cpp
    storage.cpp
    drivers/i2c_dev.cpp
    drivers/sht30.cpp
    drivers/bh1750.cpp
)

find_package(SQLite3 REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)

target_link_libraries(telemetry_core
    PUBLIC  sensornode_common
    PRIVATE SQLite::SQLite3 nlohmann_json::nlohmann_json
)

add_executable(sensornode-telemetryd
    main.cpp
)

target_link_libraries(sensornode-telemetryd
    PRIVATE
        telemetry_core
)

