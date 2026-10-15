#include <getopt.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

bool show_help     = false;
bool show_version  = false;

struct Config {
    std::string socket_path = "/run/sensornode/ota.sock";
};

void print_usage()
{
    std::fprintf(stdout,
        "Usage: sensornode-otad [options]\n"
        "\n"
        "  -s, --socket-path PATH    control socket (default: /run/sensornode/ota.sock)\n"
        "  -h, --help                show this help and exit\n"
        "  -V, --version             show version and exit\n"
    );
}

bool parse_args(int argc, char* argv[], Config& cfg)
{
    static const char short_opts[] = ":s:hv";

    static const option long_opts[] = {
        {"socket-path", required_argument, nullptr, 's'},
        {"help",        no_argument,       nullptr, 'h'},
        {"version",     no_argument,       nullptr, 'v'},
        {nullptr,       0,                 nullptr,  0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
        case 's': cfg.socket_path = optarg; break;
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

int main(int argc, char* argv[])
{
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

    std::printf("sensornode-otad %s\n", SENSORNODE_VERSION);
    std::printf("  socket_path  : %s\n", cfg.socket_path.c_str());

    return EXIT_SUCCESS;
}
