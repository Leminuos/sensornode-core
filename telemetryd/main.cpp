#include "common/event_loop.h"
#include "common/signal_fd.h"
#include "common/timer.h"

#include <getopt.h>
#include <sys/epoll.h>

#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <exception>

namespace {

bool show_help  = false;
bool show_version  = false;

struct Config {
    bool        fake_sensors = false;
    std::string socket_path  = "/run/sensornode/telemetry.sock";
    std::string db_path      = "/var/lib/sensornode/telemetry.db";
    std::chrono::milliseconds interval{5000};
};

// Thời gian chờ SHT30 đo xong. Datasheet: tối đa ~15 ms ở chế độ high
constexpr std::chrono::milliseconds kMeasureDelay{20};

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
bool parse_positive_ms(const char* s, std::chrono::milliseconds& out) {
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);

    // strtol không báo lỗi bằng giá trị trả về
    // nên phải kiểm tra: errno (tràn số), end (rác phía sau, chuỗi rỗng), khoảng.
    if (errno != 0 || end == s || *end != '\0' || v <= 0) return false;
    out = std::chrono::milliseconds{v};
    return true;
}

void print_usage(void) {
    std::fprintf(stdout,
        "Usage: sensornode-telemetryd [options]\n"
        "\n"
        "  -f, --fake-sensors        use simulated sensors instead of I2C\n"
        "  -s, --socket-path PATH    control socket (default: /run/sensornode/telemetry.sock)\n"
        "  -d, --db-path PATH        SQLite database (default: /var/lib/sensornode/telemetry.db)\n"
        "  -i, --interval-ms MS      measurement period in ms (default: 5000)\n"
        "  -h, --help                show this help\n"
        "  -v, --version             show version\n"
    );
}

bool parse_args(int argc, char* argv[], Config& cfg)
{
    static const char short_opts[] = ":fs:d:i:hv";

    static const option long_opts[] = {
        {"fake-sensors", no_argument,       nullptr, 'f'},
        {"socket-path",  required_argument, nullptr, 's'},
        {"db-path",      required_argument, nullptr, 'd'},
        {"interval-ms",  required_argument, nullptr, 'i'},
        {"help",         no_argument,       nullptr, 'h'},
        {"version",      no_argument,       nullptr, 'v'},
        {nullptr,        0,                 nullptr,  0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'f': cfg.fake_sensors = true; break;
        case 's': cfg.socket_path = optarg; break;
        case 'd': cfg.db_path = optarg; break;
        case 'i':
            if (!parse_positive_ms(optarg, cfg.interval)) {
                std::fprintf(stderr, "%s: invalid --interval-ms '%s'\n", argv[0], optarg);
                return false;
            }
            break;
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

        enum class MeasureState { Idle, Measuring };
        MeasureState state = MeasureState::Idle;
        std::uint64_t measure_count = 0;

        sensornode::Timer measure(loop, [&](std::uint64_t) {
            if (state != MeasureState::Measuring) return;
            ++measure_count;

            const double temp = 25.0 + 0.1 * static_cast<double>(measure_count % 10);
            log("measurement #%" PRIu64 " done: %.1f C", measure_count, temp);
            state = MeasureState::Idle;
        });

        sensornode::Timer tick(loop, [&](std::uint64_t expirations) {

            if (expirations > 1) {
                log("warning: missed %" PRIu64 " tick(s)", expirations - 1);
            }

            if (state == MeasureState::Measuring) {
                log("warning: previous measurement still running, skipping tick");
                return;
            }

            state = MeasureState::Measuring;
            measure.start_oneshot(kMeasureDelay);
        });

        tick.start_periodic(cfg.interval);

        loop.run();

        loop.remove(signals.getfd());
        
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "telemetryd: fatal: %s\n", e.what());
        return 1;
    }
}
