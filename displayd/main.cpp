#include "common/event_loop.h"
#include "common/log.h"
#include "common/signal_fd.h"
#include "common/systemd.h"
#include "common/timer.h"
#include "common/unix_client.h"
#include "common/unix_server.h"
#include "displayd/backlight.h"
#include "displayd/protocol.h"

#include "git_version.h"

#include <getopt.h>
#include <nlohmann/json.hpp>
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
#include <optional>
#include <string>

namespace {

using namespace std::chrono_literals;
using nlohmann::json;

constexpr int  kDefaultBrightness = 72;
constexpr auto kSettingsWait      = 1s;

bool show_help    = false;
bool show_version = false;

struct Config {
    std::string socket_path     = "/run/sensornode/display.sock";
    std::string pwm_chip        = "/sys/class/pwm/pwmchip0";
    int         pwm_channel     = 0;
    std::string settings_socket = "/run/sensornode/settings.sock";
};

std::optional<int> parse_brightness(const std::string& line) {
    const json msg = json::parse(line, nullptr, false);
    if (msg.is_discarded() || !msg.is_object() || msg.value("type", "") != "hello") return std::nullopt;

    const auto settings = msg.find("settings");
    if (settings == msg.end() || !settings->is_object()) return std::nullopt;

    const auto value = settings->find("brightness");
    if (value == settings->end() || !value->is_number_integer()) return std::nullopt;
    return value->get<int>();
}

void print_usage()
{
    std::fprintf(stdout,
        "Usage: sensornode-displayd [options]\n"
        "\n"
        "  -s, --socket-path PATH      control socket (default: /run/sensornode/display.sock)\n"
        "  -p, --pwm-chip PATH         backlight PWM chip (default: /sys/class/pwm/pwmchip0)\n"
        "  -c, --pwm-channel N         backlight PWM channel (default: 0)\n"
        "  -S, --settings-socket PATH  settingsd socket (default: /run/sensornode/settings.sock)\n"
        "  -h, --help                  show this help\n"
        "  -v, --version               show version\n"
    );
}

bool parse_args(int argc, char* argv[], Config& cfg)
{
    static const char short_opts[] = ":s:p:c:S:hv";

    static const option long_opts[] = {
        {"socket-path",     required_argument, nullptr, 's'},
        {"pwm-chip",        required_argument, nullptr, 'p'},
        {"pwm-channel",     required_argument, nullptr, 'c'},
        {"settings-socket", required_argument, nullptr, 'S'},
        {"help",            no_argument,       nullptr, 'h'},
        {"version",         no_argument,       nullptr, 'v'},
        {nullptr,           0,                 nullptr,  0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
        case 's': cfg.socket_path = optarg; break;
        case 'p': cfg.pwm_chip = optarg; break;
        case 'c': {
            errno = 0;
            char* end = nullptr;
            const long v = std::strtol(optarg, &end, 10);
            if (errno != 0 || end == optarg || *end != '\0' || v < 0 || v > 255) {
                std::fprintf(stderr, "%s: invalid --pwm-channel '%s'\n", argv[0], optarg);
                return false;
            }
            cfg.pwm_channel = static_cast<int>(v);
            break;
        }
        case 'S': cfg.settings_socket = optarg; break;
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
    try {
        sensornode::SignalFd signals{SIGTERM, SIGINT, SIGHUP};

        Config cfg;
        if (!parse_args(argc, argv, cfg)) return 2;

        if (show_help) {
            print_usage();
            return EXIT_SUCCESS;
        }

        if (show_version) {
            std::printf("sensornode-displayd %s (%s)\n", SENSORNODE_VERSION, SENSORNODE_GIT);
            return EXIT_SUCCESS;
        }

        sensornode::log::init();

        SN_LOG_NOTICE("main", "starting version=%s git=%s pid=%d", SENSORNODE_VERSION, SENSORNODE_GIT, static_cast<int>(getpid()));

        sensornode::EventLoop loop;

        loop.add(signals.getfd(), EPOLLIN, [&](std::uint32_t) {
            signals.drain([&](int signo) {
                switch (signo) {
                case SIGTERM:
                case SIGINT:
                    SN_LOG_NOTICE("main", "shutting down signal=SIG%s", sigabbrev_np(signo));
                    sensornode::systemd::notify("STOPPING=1");
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

        sensornode::display::Backlight backlight(cfg.pwm_chip, cfg.pwm_channel);
        SN_LOG_INFO("backlight", "pwm=%s/pwm%d", cfg.pwm_chip.c_str(), cfg.pwm_channel);

        // ota-confirm-boot đọc backlight= để quyết định commit slot.
        const auto apply = [&](int value) {
            const bool ok = backlight.set_brightness(value);
            SN_LOG_INFO("backlight", "brightness=%d ok=%d", value, ok);
            sensornode::systemd::notify(std::string("STATUS=backlight=") + (ok ? "ok" : "error"));
            return ok;
        };

        sensornode::Timer fallback(loop, [&](std::uint64_t) { apply(kDefaultBrightness); });
        fallback.start_oneshot(kSettingsWait);

        sensornode::UnixClient settings(loop, cfg.settings_socket);
        const auto stop_waiting = [&] {
            fallback.stop();
            settings.stop();
        };

        settings.on_line([&](const std::string& line) {
            const auto value = parse_brightness(line);
            if (!value) return;
            stop_waiting();
            apply(*value);
        });
        settings.start();

        sensornode::UnixServer::Options sopts;
        sopts.path = cfg.socket_path;
        sensornode::UnixServer server(loop, sopts);
        SN_LOG_INFO("ipc", "listening path=%s", sopts.path.c_str());

        server.on_connect([&](sensornode::UnixServer::ClientId id, const sensornode::PeerCred& p) {
            SN_LOG_INFO("ipc", "client connected cid=%" PRIu64 " pid=%d uid=%d gid=%d", id,
                        static_cast<int>(p.pid), static_cast<int>(p.uid), static_cast<int>(p.gid));
            server.send(id, sensornode::display::make_hello());
            return true;
        });

        server.on_disconnect([&](sensornode::UnixServer::ClientId id, const char* reason) {
            SN_LOG_INFO("ipc", "client disconnected: %s cid=%" PRIu64, reason, id);
        });

        server.on_line([&](sensornode::UnixServer::ClientId id, const std::string& line) {
            server.send(id, sensornode::display::handle_command(line, [&](int value) {
                stop_waiting();
                return apply(value);
            }));
        });

        sensornode::Watchdog watchdog(loop, [] { return true; });

        SN_LOG_NOTICE("main", "ready");
        sensornode::systemd::notify("READY=1");
        loop.run();

        loop.remove(signals.getfd());

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        SN_LOG_ERR("main", "fatal: %s", e.what());
        return 1;
    }
}
