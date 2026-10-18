#pragma once

#include "telemetryd/sensors.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace sensornode {

constexpr std::chrono::hours    kRawRetention{24 * 7};
constexpr std::chrono::hours    kHourlyRetention{24 * 365};
constexpr std::chrono::minutes  kMaintenanceInterval{60};

struct MaintenanceStats {
    int hours_aggregated = 0;
    int raw_purged       = 0;
    int hourly_purged    = 0;
};

// Daemon là writer duy nhất, UI mở DB read-only để đọc lịch sử.
class Storage {
public:
    explicit Storage(const std::string& path);
    ~Storage();

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    bool insert(const Sample& s);
    bool maintenance(std::int64_t now_ms, MaintenanceStats& stats);

    const char* errmsg() const;

private:
    struct DbClose   { void operator()(sqlite3* db) const; };
    struct StmtFinal { void operator()(sqlite3_stmt* st) const; };
    using Stmt = std::unique_ptr<sqlite3_stmt, StmtFinal>;

    std::unique_ptr<sqlite3, DbClose> db_;
    Stmt insert_;
    Stmt last_hour_;
    Stmt aggregate_;
    Stmt purge_raw_;
    Stmt purge_hourly_;
    std::string last_error_;

    void open(const std::string& path);
    void configure();
    void migrate();

    int  exec_sql(const std::string& sql);
    Stmt prepare(const char* sql);
    bool step_done(sqlite3_stmt* stmt);
};

std::int64_t wall_now_ms();

} // namespace sensornode
