#include "telemetryd/storage.h"

#include <sqlite3.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace sensornode {

namespace {

constexpr std::int64_t kHourMs = 3600 * 1000;

constexpr int kSchemaVersion = 1;

constexpr const char* kSchemaV1 = R"sql(
    CREATE TABLE samples (
        id      INTEGER PRIMARY KEY,
        ts      INTEGER NOT NULL,       -- unix ms
        temp_c  REAL,                   -- NULL khi cảm biến lỗi
        humi_rh REAL,
        lux     REAL
    );
    CREATE INDEX samples_ts ON samples(ts);

    CREATE TABLE samples_hourly (
        hour_ts  INTEGER PRIMARY KEY,   -- unix ms
        n        INTEGER NOT NULL,
        temp_avg REAL, temp_min REAL, temp_max REAL,
        humi_avg REAL, humi_min REAL, humi_max REAL,
        lux_avg  REAL, lux_min  REAL, lux_max  REAL
    );
)sql";

constexpr const char* kInsertSql =
    "INSERT INTO samples (ts, temp_c, humi_rh, lux) VALUES (?1, ?2, ?3, ?4)";

constexpr const char* kLastHourSql = R"sql(
    SELECT COALESCE(MAX(hour_ts) + 3600000, 0) FROM samples_hourly
)sql";

constexpr const char* kAggregateSql = R"sql(
    INSERT OR REPLACE INTO samples_hourly
        (hour_ts, n,
         temp_avg, temp_min, temp_max,
         humi_avg, humi_min, humi_max,
         lux_avg,  lux_min,  lux_max)
    SELECT ts / 3600000 * 3600000 AS h, COUNT(*),
           AVG(temp_c),  MIN(temp_c),  MAX(temp_c),
           AVG(humi_rh), MIN(humi_rh), MAX(humi_rh),
           AVG(lux),     MIN(lux),     MAX(lux)
    FROM samples
    WHERE ts >= ?1 AND ts < ?2
    GROUP BY h
)sql";

constexpr const char* kPurgeRawSql = R"sql(
    DELETE FROM samples
    WHERE ts < ?1
)sql";

constexpr const char* kPurgeHourlySql = R"sql(
    DELETE FROM samples_hourly
    WHERE hour_ts < ?1
)sql";

std::int64_t to_ms(std::chrono::hours h) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(h).count();
}

void bind_reading(sqlite3_stmt* st, int idx, double value, bool valid) {
    if (valid) sqlite3_bind_double(st, idx, value);
    else       sqlite3_bind_null(st, idx);
}

} // namespace

void Storage::DbClose::operator()(sqlite3* db) const { sqlite3_close(db); }
void Storage::StmtFinal::operator()(sqlite3_stmt* st) const { sqlite3_finalize(st); }

Storage::Storage(const std::string& path) {
    open(path);
    configure();
    migrate();

    insert_       = prepare(kInsertSql);
    last_hour_    = prepare(kLastHourSql);
    aggregate_    = prepare(kAggregateSql);
    purge_raw_    = prepare(kPurgeRawSql);
    purge_hourly_ = prepare(kPurgeHourlySql);
}

Storage::~Storage() = default;

bool Storage::insert(const Sample& s) {
    sqlite3_stmt* st = insert_.get();

    sqlite3_bind_int64(st, 1, s.ts_ms);
    bind_reading(st, 2, s.temp_c,  !s.failed(kSht30));
    bind_reading(st, 3, s.humi_rh, !s.failed(kSht30));
    bind_reading(st, 4, s.lux,     !s.failed(kBh1750));

    return step_done(st);
}

bool Storage::maintenance(std::int64_t now_ms, MaintenanceStats& stats) {
    stats = MaintenanceStats{};

    const std::int64_t current_hour  = now_ms / kHourMs * kHourMs;
    const std::int64_t raw_cutoff    = now_ms - to_ms(kRawRetention);
    const std::int64_t hourly_cutoff = now_ms - to_ms(kHourlyRetention);

    // Gộp và xoá trong một giao dịch để không xoá mẫu chưa kịp gộp.
    if (exec_sql("BEGIN IMMEDIATE") != SQLITE_OK) {
        return false;
    }

    std::int64_t first_hour = 0;
    if (sqlite3_step(last_hour_.get()) == SQLITE_ROW) {
        first_hour = sqlite3_column_int64(last_hour_.get(), 0);
    }
    sqlite3_reset(last_hour_.get());

    // Bước 1: gộp các giờ đã trọn vẹn vào samples_hourly.
    sqlite3_bind_int64(aggregate_.get(), 1, first_hour);
    sqlite3_bind_int64(aggregate_.get(), 2, current_hour);
    if (!step_done(aggregate_.get())) {
        exec_sql("ROLLBACK");
        return false;
    }
    stats.hours_aggregated = sqlite3_changes(db_.get());

    // Bước 2: xoá dữ liệu thô quá kRawRetention.
    sqlite3_bind_int64(purge_raw_.get(), 1, raw_cutoff);
    if (!step_done(purge_raw_.get())) {
        exec_sql("ROLLBACK");
        return false;
    }
    stats.raw_purged = sqlite3_changes(db_.get());

    // Bước 3: xoá dữ liệu theo giờ quá kHourlyRetention.
    sqlite3_bind_int64(purge_hourly_.get(), 1, hourly_cutoff);
    if (!step_done(purge_hourly_.get())) {
        exec_sql("ROLLBACK");
        return false;
    }
    stats.hourly_purged = sqlite3_changes(db_.get());

    if (exec_sql("COMMIT") != SQLITE_OK) {
        exec_sql("ROLLBACK");
        return false;
    }

    return true;
}

const char* Storage::errmsg() const {
    return last_error_.c_str();
}

void Storage::open(const std::string& path) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &raw,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                   nullptr);
    db_.reset(raw);
    if (rc == SQLITE_OK) return;

    std::string msg = "open " + path + ": " + sqlite3_errmsg(raw);
    throw std::runtime_error(msg);
}

void Storage::configure() {
    sqlite3_extended_result_codes(db_.get(), 1);
    sqlite3_busy_timeout(db_.get(), 1000);

    // Không bật được WAL thì SQLite giữ mode cũ mà không báo lỗi.
    Stmt stmt = prepare("PRAGMA journal_mode=WAL");

    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_ROW) {
        throw std::runtime_error(std::string("PRAGMA journal_mode: ") + sqlite3_errmsg(db_.get()));
    }

    const unsigned char* text = sqlite3_column_text(stmt.get(), 0);
    const std::string mode = text ? reinterpret_cast<const char*>(text) : "";
    if (mode != "wal") {
        throw std::runtime_error("cannot enable WAL, journal mode is '" + mode + "'");
    }

    // Chỉ fsync lúc checkpoint, đỡ mòn thẻ SD.
    if (exec_sql("PRAGMA synchronous=NORMAL") != SQLITE_OK) {
        throw std::runtime_error(last_error_);
    }
}

void Storage::migrate() {
    int version = 0;
    {
        Stmt st = prepare("PRAGMA user_version");
        if (sqlite3_step(st.get()) == SQLITE_ROW) version = sqlite3_column_int(st.get(), 0);
    }

    if (version >= kSchemaVersion) return;

    bool ok = exec_sql("BEGIN IMMEDIATE") == SQLITE_OK;
    if (ok && version < 1) ok = exec_sql(kSchemaV1) == SQLITE_OK;
    if (ok) ok = exec_sql("PRAGMA user_version = " + std::to_string(kSchemaVersion)) == SQLITE_OK;
    if (ok) ok = exec_sql("COMMIT") == SQLITE_OK;

    if (!ok) {
        const std::string err = last_error_;
        exec_sql("ROLLBACK");
        throw std::runtime_error("migrate to v" + std::to_string(kSchemaVersion) + ": " + err);
    }
}

int Storage::exec_sql(const std::string& sql) {
    char* errmsg = nullptr;

    const int rc = sqlite3_exec(db_.get(), sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        last_error_ = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg); // errmsg do SQLite cấp phát, phải free
    }

    return rc;
}

Storage::Stmt Storage::prepare(const char* sql) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_.get(), sql, -1, &st, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("prepare: ") + sqlite3_errmsg(db_.get()));
    }
    return Stmt(st);
}

bool Storage::step_done(sqlite3_stmt* stmt) {
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) last_error_ = sqlite3_errmsg(db_.get());

    sqlite3_reset(stmt);
    return ok;
}

std::int64_t wall_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace sensornode
