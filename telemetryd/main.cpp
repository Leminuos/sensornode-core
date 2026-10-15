
#include <getopt.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

bool show_help  = false;
bool show_version  = false;

struct Config {
    bool        fake_sensors = false;
    std::string socket_path  = "/run/sensornode/telemetry.sock";
    std::string db_path      = "/var/lib/sensornode/telemetry.db";
};

void print_usage(void) {
    std::fprintf(stdout,
        "Usage: sensornode-telemetryd [options]\n"
        "\n"
        "  -f, --fake-sensors        use simulated sensors instead of I2C\n"
        "  -s, --socket-path PATH    control socket (default: /run/sensornode/telemetry.sock)\n"
        "  -d, --db-path PATH        SQLite database (default: /var/lib/sensornode/telemetry.db)\n"
        "  -h, --help                show this help\n"
        "  -v, --version             show version\n"
    );
}

bool parse_args(int argc, char* argv[], Config& cfg)
{
    static const char short_opts[] = ":fs:d:hv";

    static const option long_opts[] = {
        {"fake-sensors", no_argument,       nullptr, 'f'},
        {"socket-path",  required_argument, nullptr, 's'},
        {"db-path",      required_argument, nullptr, 'd'},
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

    std::printf("sensornode-telemetryd %s\n", SENSORNODE_VERSION);
    std::printf("  fake_sensors : %s\n", cfg.fake_sensors ? "yes" : "no");
    std::printf("  socket_path  : %s\n", cfg.socket_path.c_str());
    std::printf("  db_path      : %s\n", cfg.db_path.c_str());

    return EXIT_SUCCESS;
}
