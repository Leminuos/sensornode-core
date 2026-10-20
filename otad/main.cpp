#include "common/event_loop.h"
#include "common/log.h"
#include "common/signal_fd.h"
#include "common/systemd.h"
#include "common/unix_server.h"
#include "otad/protocol.h"

#include "git_version.h"

#include <getopt.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <cinttypes>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace {

bool show_help    = false;
bool show_version = false;

struct Config {
    std::string socket_path = "/run/sensornode/ota.sock";
};

void print_usage()
{
    std::fprintf(stdout,
        "Usage: sensornode-otad [options]\n"
        "\n"
        "  -s, --socket-path PATH    control socket (default: /run/sensornode/ota.sock)\n"
        "  -h, --help                show this help\n"
        "  -v, --version             show version\n"
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
    try {
        sensornode::SignalFd signals{SIGTERM, SIGINT, SIGHUP};

        Config cfg;
        if (!parse_args(argc, argv, cfg)) return 2;

        if (show_help) {
            print_usage();
            return EXIT_SUCCESS;
        }

        if (show_version) {
            std::printf("sensornode-otad %s (%s)\n", SENSORNODE_VERSION, SENSORNODE_GIT);
            return EXIT_SUCCESS;
        }

        sensornode::log::init();

        SN_LOG_NOTICE("main", "starting version=%s git=%s pid=%d",
                      SENSORNODE_VERSION, SENSORNODE_GIT, static_cast<int>(getpid()));

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

        sensornode::UnixServer::Options sopts;
        sopts.path = cfg.socket_path;
        sensornode::UnixServer server(loop, sopts);
        SN_LOG_INFO("ipc", "listening path=%s", sopts.path.c_str());

        server.on_connect([&](sensornode::UnixServer::ClientId id, const sensornode::PeerCred& p) {
            SN_LOG_INFO("ipc", "client connected cid=%" PRIu64 " pid=%d uid=%d gid=%d", id,
                        static_cast<int>(p.pid), static_cast<int>(p.uid), static_cast<int>(p.gid));
            server.send(id, sensornode::ota::make_hello());
            return true;
        });

        server.on_disconnect([&](sensornode::UnixServer::ClientId id, const char* reason) {
            SN_LOG_INFO("ipc", "client disconnected: %s cid=%" PRIu64, reason, id);
        });

        server.on_line([&](sensornode::UnixServer::ClientId id, const std::string& line) {
            SN_LOG_DEBUG("ipc", "line received cid=%" PRIu64 " line=%s", id,
                         sensornode::log::quote(line).c_str());
            server.send(id, sensornode::ota::handle_command(line));
        });

        // Chưa có việc background nào để kiểm tra, tick chạy được nghĩa là loop không bị kẹt.
        sensornode::Watchdog watchdog(loop, [] { return true; });

        SN_LOG_NOTICE("main", "ready");
        sensornode::systemd::notify("READY=1");
        sensornode::systemd::notify("STATUS=state=idle");
        loop.run();

        loop.remove(signals.getfd());

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        SN_LOG_ERR("main", "fatal: %s", e.what());
        return 1;
    }
}
