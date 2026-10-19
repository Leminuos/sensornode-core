#include "common/event_loop.h"
#include "common/signal_fd.h"
#include "common/timer.h"
#include "common/unix_server.h"
#include "telemetryd/mqtt_client.h"
#include "telemetryd/protocol.h"
#include "telemetryd/sensors.h"
#include "telemetryd/storage.h"

#include <getopt.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdarg>
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

// In log kèm mốc thời gian monotonic
void log(const char* fmt, ...) {
    using namespace std::chrono;
    const auto now = std::chrono::duration_cast<milliseconds>(sensornode::monotonic_now()).count();
    std::printf("[%6lld.%03lld] telemetryd: ",
                 static_cast<long long>(now / 1000), static_cast<long long>(now % 1000));
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}

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

void log_sample(const sensornode::Sample& s) {
    log("temperature=%.2f C  humidity=%.2f %%RH  lux=%.2f lx",
        s.temp_c, s.humi_rh, s.lux);

    for (const auto& e : s.errors) {
        log("warning: %s: %s", e.sensor, std::strerror(-e.err));
    }
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
            std::printf("sensornode-telemetryd %s\n", SENSORNODE_VERSION);
            return EXIT_SUCCESS;
        }

        sensornode::EventLoop loop;

        loop.add(signals.getfd(), EPOLLIN, [&](std::uint32_t) {
            signals.drain([&](int signo) {
                switch (signo) {
                case SIGTERM:
                case SIGINT:
                    std::fprintf(stderr, "telemetryd: got %s, shutting down\n", strsignal(signo));
                    loop.stop();
                    break;
                case SIGHUP:
                    std::fprintf(stderr, "telemetryd: got SIGHUP, reload not supported yet\n");
                    break;
                default:
                    std::fprintf(stderr, "telemetryd: unexpected signal %d\n", signo);
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
        log("database: %s", cfg.db_path.c_str());

        bool have_latest = false;
        sensornode::Sample latest;

        sensornode::UnixServer::Options sopts;
        sopts.path = cfg.socket_path;
        sensornode::UnixServer server(loop, sopts);
        log("listening on %s", cfg.socket_path.c_str());

        server.on_connect([&](sensornode::UnixServer::ClientId id, const sensornode::PeerCred& p) {
            log("client %" PRIu64 " connected: pid=%d uid=%d gid=%d", id,
                static_cast<int>(p.pid), static_cast<int>(p.uid), static_cast<int>(p.gid));
            server.send(id, sensornode::make_hello(cfg.interval));
            return true;
        });

        server.on_disconnect([&](sensornode::UnixServer::ClientId id, const char* reason) {
            log("client %" PRIu64 " disconnected: %s", id, reason);
        });

        server.on_line([&](sensornode::UnixServer::ClientId id, const std::string& line) {
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
                log("mqtt: connected to %s:%d", cfg.mqtt_host.c_str(), cfg.mqtt_port);
                mqtt->publish(status_topic, "online", 1, true);
            });
            mqtt->on_disconnect([&](const char* reason, std::chrono::seconds retry_in) {
                log("mqtt: %s, retry in %lld s", reason, static_cast<long long>(retry_in.count()));
            });

            log("mqtt: broker %s:%d, topic %s/#", cfg.mqtt_host.c_str(), cfg.mqtt_port, topic_base.c_str());
            mqtt->start();
        } else {
            log("mqtt: disabled (no --mqtt-host)");
        }

        bool measuring = false;

        sensornode::Timer measure_done(loop, [&](std::uint64_t) {
            measuring = false;
            sensornode::Sample s;
            s.ts_ms = sensornode::wall_now_ms();
            sensors->fetch(s);
            log_sample(s);

            if (!storage.insert(s)) {
                log("error: database: %s", storage.errmsg());
            }

            server.broadcast(sensornode::make_sample_event(s));
            if (mqtt) mqtt->publish(telemetry_topic, sensornode::make_sample_payload(s), 1, false);
            latest = std::move(s);
            have_latest = true;
        });

        const auto start_measurement = [&]() {
            if (measuring) {
                log("warning: previous measurement still running, skipping tick");
                return;
            }
            sensors->start();
            measuring = true;
            measure_done.start_oneshot(sensornode::kMeasureTime);
        };

        sensornode::Timer tick(loop, [&](std::uint64_t expirations) {
            if (expirations > 1) {
                log("warning: missed %" PRIu64 " tick(s)", expirations - 1);
            }
            start_measurement();
        });

        const auto run_maintenance = [&]() {
            const auto t0 = sensornode::monotonic_now();
            sensornode::MaintenanceStats st;
            if (!storage.maintenance(sensornode::wall_now_ms(), st)) {
                log("error: database maintenance: %s", storage.errmsg());
                return;
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                sensornode::monotonic_now() - t0).count();
            log("database maintenance: %d hour(s) aggregated, %d raw / %d hourly row(s) purged, %lld ms",
                st.hours_aggregated, st.raw_purged, st.hourly_purged, static_cast<long long>(ms));
        };

        sensornode::Timer maintenance(loop, [&](std::uint64_t) { run_maintenance(); });

        // Chạy ngay khi khởi động, không đợi hết chu kỳ đầu.
        start_measurement();
        tick.start_periodic(cfg.interval);
        run_maintenance();
        maintenance.start_periodic(sensornode::kMaintenanceInterval);

        loop.run();

        if (mqtt) mqtt->stop();

        loop.remove(signals.getfd());
        
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "telemetryd: fatal: %s\n", e.what());
        return 1;
    }
}
