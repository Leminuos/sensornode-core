#include "common/event_loop.h"
#include "common/log.h"
#include "common/signal_fd.h"
#include "common/timer.h"
#include "common/unix_server.h"
#include "telemetryd/mqtt_client.h"
#include "telemetryd/protocol.h"
#include "telemetryd/sensors.h"
#include "telemetryd/storage.h"

#include "git_version.h"

#include <getopt.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

namespace {

bool show_help  = false;
bool show_version  = false;

struct Config {
    bool        fake_sensors = false;
    std::string i2c_bus      = "/dev/i2c-2";
    std::string socket_path  = "/run/sensornode/telemetry.sock";
    std::string db_path      = "/var/lib/sensornode/telemetry.db";
    std::string mqtt_host;
    int         mqtt_port    = 1883;
    std::string node_id;
    std::chrono::milliseconds interval{5000};
};

// Đọc một số nguyên dương từ chuỗi.
bool parse_positive(const char* s, long& out) {
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);

    // strtol không báo lỗi bằng giá trị trả về
    // nên phải kiểm tra: errno (tràn số), end (rác phía sau, chuỗi rỗng), khoảng.
    if (errno != 0 || end == s || *end != '\0' || v <= 0) return false;
    out = v;
    return true;
}

void print_usage(void) {
    std::fprintf(stdout,
        "Usage: sensornode-telemetryd [options]\n"
        "\n"
        "  -f, --fake-sensors        use simulated sensors instead of I2C\n"
        "  -b, --i2c-bus PATH        I2C bus of the sensors (default: /dev/i2c-2)\n"
        "  -s, --socket-path PATH    control socket (default: /run/sensornode/telemetry.sock)\n"
        "  -d, --db-path PATH        SQLite database (default: /var/lib/sensornode/telemetry.db)\n"
        "  -i, --interval-ms MS      measurement period in ms (default: 5000)\n"
        "  -m, --mqtt-host HOST      MQTT broker, IP preferred (default: MQTT disabled)\n"
        "  -p, --mqtt-port PORT      MQTT broker port (default: 1883)\n"
        "  -n, --node-id ID          node id in MQTT topics (default: hostname)\n"
        "  -h, --help                show this help\n"
        "  -v, --version             show version\n"
    );
}

bool parse_args(int argc, char* argv[], Config& cfg)
{
    static const char short_opts[] = ":fb:s:d:i:m:p:n:hv";

    static const option long_opts[] = {
        {"fake-sensors", no_argument,       nullptr, 'f'},
        {"i2c-bus",      required_argument, nullptr, 'b'},
        {"socket-path",  required_argument, nullptr, 's'},
        {"db-path",      required_argument, nullptr, 'd'},
        {"interval-ms",  required_argument, nullptr, 'i'},
        {"mqtt-host",    required_argument, nullptr, 'm'},
        {"mqtt-port",    required_argument, nullptr, 'p'},
        {"node-id",      required_argument, nullptr, 'n'},
        {"help",         no_argument,       nullptr, 'h'},
        {"version",      no_argument,       nullptr, 'v'},
        {nullptr,        0,                 nullptr,  0 },
    };

    int opt;
    long v;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'f': cfg.fake_sensors = true; break;
        case 'b': cfg.i2c_bus = optarg; break;
        case 's': cfg.socket_path = optarg; break;
        case 'd': cfg.db_path = optarg; break;
        case 'i':
            if (!parse_positive(optarg, v)) {
                std::fprintf(stderr, "%s: invalid --interval-ms '%s'\n", argv[0], optarg);
                return false;
            }
            cfg.interval = std::chrono::milliseconds{v};
            break;
        case 'm': cfg.mqtt_host = optarg; break;
        case 'p':
            if (!parse_positive(optarg, v) || v > 65535) {
                std::fprintf(stderr, "%s: invalid --mqtt-port '%s'\n", argv[0], optarg);
                return false;
            }
            cfg.mqtt_port = static_cast<int>(v);
            break;
        case 'n': cfg.node_id = optarg; break;
        case 'h': show_help = true; break;
        case 'v': show_version = true; break;
        case ':':
            std::fprintf(stderr, "%s: option '%s' requires an argument\n", argv[0], argv[optind - 1]);
            print_usage();
            return false;
        case '?':
        default:
            std::fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[optind - 1]);
            print_usage();
            return false;
        }
    }

    if (optind < argc) {
        std::fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[optind]);
        print_usage();
        return false;
    }

    // Interval ngắn hơn thời gian đo thì tick nào cũng bị bỏ qua.
    if (cfg.interval <= sensornode::kMeasureTime) {
        std::fprintf(stderr, "%s: --interval-ms must be greater than %lld\n",
                     argv[0], static_cast<long long>(sensornode::kMeasureTime.count()));
        return false;
    }

    if (cfg.node_id.empty()) {
        char host[256] = {};
        if (gethostname(host, sizeof host - 1) == 0) cfg.node_id = host;
    }
    // Ký tự wildcard/phân cấp của MQTT không được nằm trong một level của topic.
    if (cfg.node_id.empty() || cfg.node_id.find_first_of("/+#") != std::string::npos) {
        std::fprintf(stderr, "%s: invalid --node-id '%s'\n", argv[0], cfg.node_id.c_str());
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        sensornode::SignalFd signals{SIGTERM, SIGINT, SIGHUP};

        Config cfg;
        if (!parse_args(argc, argv, cfg)) return 2;

        if (show_help) {
            print_usage();
            return EXIT_SUCCESS;
        }

        if (show_version) {
            std::printf("sensornode-telemetryd %s (%s)\n", SENSORNODE_VERSION, SENSORNODE_GIT);
            return EXIT_SUCCESS;
        }

        sensornode::log::init();

        SN_LOG_NOTICE("main", "starting version=%s git=%s pid=%d",
                      SENSORNODE_VERSION, SENSORNODE_GIT, static_cast<int>(getpid()));
        SN_LOG_INFO("main", "config node=%s interval=%lldms log_level=%s",
                    cfg.node_id.c_str(), static_cast<long long>(cfg.interval.count()),
                    sensornode::log::level_name(sensornode::log::level()));

        sensornode::EventLoop loop;

        loop.add(signals.getfd(), EPOLLIN, [&](std::uint32_t) {
            signals.drain([&](int signo) {
                switch (signo) {
                case SIGTERM:
                case SIGINT:
                    SN_LOG_NOTICE("main", "shutting down signal=SIG%s", sigabbrev_np(signo));
                    loop.stop();
                    break;
                case SIGHUP:
                    SN_LOG_NOTICE("main", "reload not supported, ignored signal=SIGHUP");
                    break;
                default:
                    SN_LOG_WARN("main", "unexpected signal signo=%d", signo);
                    break;
                }
            });
        });

        std::unique_ptr<sensornode::Sensors> sensors;
        if (cfg.fake_sensors) {
            sensors = sensornode::make_fake_sensors();
        } else {
            sensors = sensornode::make_i2c_sensors(cfg.i2c_bus);
        }

        sensornode::Storage storage(cfg.db_path);
        SN_LOG_INFO("db", "opened path=%s", cfg.db_path.c_str());

        bool have_latest = false;
        sensornode::Sample latest;

        sensornode::UnixServer::Options sopts;
        sopts.path = cfg.socket_path;
        sensornode::UnixServer server(loop, sopts);
        SN_LOG_INFO("ipc", "listening path=%s", sopts.path.c_str());

        server.on_connect([&](sensornode::UnixServer::ClientId id, const sensornode::PeerCred& p) {
            SN_LOG_INFO("ipc", "client connected cid=%" PRIu64 " pid=%d uid=%d gid=%d", id,
                        static_cast<int>(p.pid), static_cast<int>(p.uid), static_cast<int>(p.gid));
            server.send(id, sensornode::make_hello(cfg.interval));
            return true;
        });

        server.on_disconnect([&](sensornode::UnixServer::ClientId id, const char* reason) {
            SN_LOG_INFO("ipc", "client disconnected: %s cid=%" PRIu64, reason, id);
        });

        server.on_line([&](sensornode::UnixServer::ClientId id, const std::string& line) {
            SN_LOG_DEBUG("ipc", "line received cid=%" PRIu64 " line=%s", id,
                         sensornode::log::quote(line).c_str());
            server.send(id, sensornode::handle_command(line, have_latest ? &latest : nullptr));
        });

        std::unique_ptr<sensornode::MqttClient> mqtt;
        const std::string topic_base      = "sensornode/" + cfg.node_id;
        const std::string status_topic    = topic_base + "/status";
        const std::string telemetry_topic = topic_base + "/telemetry";

        if (!cfg.mqtt_host.empty()) {
            sensornode::MqttClient::Options mopts;
            mopts.host         = cfg.mqtt_host;
            mopts.port         = cfg.mqtt_port;
            mopts.client_id    = "telemetryd-" + cfg.node_id;
            mopts.will_topic   = status_topic;
            mopts.will_payload = "offline";
            mqtt = std::make_unique<sensornode::MqttClient>(loop, mopts);

            mqtt->on_connect([&]() {
                SN_LOG_INFO("mqtt", "connected host=%s port=%d", cfg.mqtt_host.c_str(), cfg.mqtt_port);
                mqtt->publish(status_topic, "online", 1, true);
            });
            mqtt->on_disconnect([&](const char* reason, std::chrono::seconds retry_in) {
                SN_LOG_WARN("mqtt", "disconnected: %s retry_in=%llds", reason,
                            static_cast<long long>(retry_in.count()));
            });

            SN_LOG_INFO("mqtt", "broker host=%s port=%d client_id=%s topic=%s/#",
                        cfg.mqtt_host.c_str(), cfg.mqtt_port, mopts.client_id.c_str(), topic_base.c_str());
            mqtt->start();
        } else {
            SN_LOG_INFO("mqtt", "disabled reason=\"no --mqtt-host\"");
        }

        bool measuring = false;

        sensornode::Timer measure_done(loop, [&](std::uint64_t) {
            measuring = false;
            sensornode::Sample s;
            s.ts_ms = sensornode::wall_now_ms();
            sensors->fetch(s);
            SN_LOG_DEBUG("sensor", "sample ts_ms=%" PRId64 " temp_c=%.2f humi_rh=%.2f lux=%.2f",
                         s.ts_ms, s.temp_c, s.humi_rh, s.lux);

            if (!storage.insert(s)) {
                SN_LOG_ERR("db", "insert failed: %s ts_ms=%" PRId64, storage.errmsg(), s.ts_ms);
            }

            server.broadcast(sensornode::make_sample_event(s));
            if (mqtt) mqtt->publish(telemetry_topic, sensornode::make_sample_payload(s), 1, false);
            latest = std::move(s);
            have_latest = true;
        });

        const auto start_measurement = [&]() {
            if (measuring) {
                SN_LOG_WARN("sensor", "tick skipped: previous measurement still running");
                return;
            }
            sensors->start();
            measuring = true;
            measure_done.start_oneshot(sensornode::kMeasureTime);
        };

        sensornode::Timer tick(loop, [&](std::uint64_t expirations) {
            if (expirations > 1) {
                SN_LOG_WARN("main", "ticks missed count=%" PRIu64 " interval=%lldms",
                            expirations - 1, static_cast<long long>(cfg.interval.count()));
            }
            start_measurement();
        });

        const auto run_maintenance = [&]() {
            const auto t0 = sensornode::monotonic_now();
            sensornode::MaintenanceStats st;
            if (!storage.maintenance(sensornode::wall_now_ms(), st)) {
                SN_LOG_ERR("db", "maintenance failed: %s", storage.errmsg());
                return;
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                sensornode::monotonic_now() - t0).count();
            SN_LOG_INFO("db", "maintenance done hours_aggregated=%d raw_purged=%d hourly_purged=%d took=%lldms",
                        st.hours_aggregated, st.raw_purged, st.hourly_purged, static_cast<long long>(ms));
        };

        sensornode::Timer maintenance(loop, [&](std::uint64_t) { run_maintenance(); });

        // Chạy ngay khi khởi động, không đợi hết chu kỳ đầu.
        start_measurement();
        tick.start_periodic(cfg.interval);
        run_maintenance();
        maintenance.start_periodic(sensornode::kMaintenanceInterval);

        SN_LOG_NOTICE("main", "ready");
        loop.run();

        if (mqtt) mqtt->stop();

        loop.remove(signals.getfd());
        
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        SN_LOG_ERR("main", "fatal: %s", e.what());
        return 1;
    }
}
