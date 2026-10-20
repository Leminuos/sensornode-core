#include "common/event_loop.h"
#include "common/log.h"
#include "common/signal_fd.h"
#include "common/systemd.h"
#include "common/unix_server.h"
#include "otad/installer.h"
#include "otad/ota_manager.h"
#include "otad/protocol.h"
#include "otad/system_info.h"

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
#include <memory>
#include <optional>
#include <string>

namespace {

bool show_help    = false;
bool show_version = false;

const char* or_unknown(const std::optional<std::string>& v) {
    return v ? v->c_str() : "?";
}

std::string or_unknown(const std::optional<int>& v) {
    return v ? std::to_string(*v) : "?";
}

struct Config {
    bool        fake_installer = false;
    std::string socket_path    = "/run/sensornode/ota.sock";
    std::string swupdate_tool  = "/usr/bin/09-swupdate-args";
    sensornode::ota::SystemInfo::Paths sys;
};

void print_usage()
{
    std::fprintf(stdout,
        "Usage: sensornode-otad [options]\n"
        "\n"
        "  -f, --fake-installer      simulate download/install instead of running SWUpdate\n"
        "  -s, --socket-path PATH    control socket (default: /run/sensornode/ota.sock)\n"
        "  -t, --swupdate-tool PATH  update launcher, run as 'PATH <url>' (default: /usr/bin/09-swupdate-args)\n"
        "  -w, --sw-versions PATH    firmware version file (default: /etc/sw-versions)\n"
        "  -e, --fw-env-config PATH  U-Boot env config (default: /etc/fw_env.config)\n"
        "  -k, --cmdline PATH        kernel command line (default: /proc/cmdline)\n"
        "  -h, --help                show this help\n"
        "  -v, --version             show version\n"
    );
}

bool parse_args(int argc, char* argv[], Config& cfg)
{
    static const char short_opts[] = ":fs:t:w:e:k:hv";

    static const option long_opts[] = {
        {"fake-installer", no_argument,       nullptr, 'f'},
        {"socket-path",    required_argument, nullptr, 's'},
        {"swupdate-tool",  required_argument, nullptr, 't'},
        {"sw-versions",    required_argument, nullptr, 'w'},
        {"fw-env-config",  required_argument, nullptr, 'e'},
        {"cmdline",        required_argument, nullptr, 'k'},
        {"help",           no_argument,       nullptr, 'h'},
        {"version",        no_argument,       nullptr, 'v'},
        {nullptr,          0,                 nullptr,  0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'f': cfg.fake_installer = true; break;
        case 's': cfg.socket_path = optarg; break;
        case 't': cfg.swupdate_tool = optarg; break;
        case 'w': cfg.sys.sw_versions = optarg; break;
        case 'e': cfg.sys.fw_env_config = optarg; break;
        case 'k': cfg.sys.cmdline = optarg; break;
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

        const sensornode::ota::SystemInfo sys(cfg.sys);
        const auto st = sys.read();
        SN_LOG_INFO("sys", "firmware sw_version=%s running_slot=%s active_slot=%s ustate=%s boot_count=%s boot_limit=%s",
                    or_unknown(st.sw_version), or_unknown(st.running_slot), or_unknown(st.active_slot),
                    or_unknown(st.ustate).c_str(), or_unknown(st.boot_count).c_str(),
                    or_unknown(st.boot_limit).c_str());
        // Không đọc được vẫn chạy tiếp để UI còn hỏi được status và thấy lỗi.
        for (const auto& e : st.errors) {
            SN_LOG_WARN("sys", "read failed source=%s: %s", e.source, e.message.c_str());
        }

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

        std::unique_ptr<sensornode::ota::Installer> installer;
        if (cfg.fake_installer) {
            installer = sensornode::ota::make_fake_installer(loop);
            SN_LOG_INFO("ota", "installer=fake");
        } else {
            installer = sensornode::ota::make_swupdate_installer(loop, cfg.swupdate_tool);
            SN_LOG_INFO("ota", "installer=swupdate tool=%s", cfg.swupdate_tool.c_str());
        }

        // SWUpdate đã ghi slot kia và chưa reboot, otad restart giữa chừng vẫn phải báo done.
        const bool reboot_pending = st.running_slot && st.active_slot && st.ustate == 1 &&
                                    *st.running_slot != *st.active_slot;

        sensornode::ota::OtaManager ota(std::move(installer), reboot_pending);
        SN_LOG_INFO("ota", "initial state=%s", sensornode::ota::to_string(ota.snapshot().state));

        const auto update_status = [&]() {
            sensornode::systemd::notify(
                std::string("STATUS=state=") + sensornode::ota::to_string(ota.snapshot().state) +
                " sw_version=" + or_unknown(st.sw_version) + " slot=" + or_unknown(st.running_slot));
        };

        ota.on_state([&](const sensornode::ota::OtaManager::Snapshot& s) {
            server.broadcast(sensornode::ota::make_state_event(s));
            update_status();
        });

        // STATUS chỉ đổi theo state, progress gửi lên systemd quá dày không có ích.
        ota.on_progress([&](const sensornode::ota::OtaManager::Snapshot& s) {
            server.broadcast(sensornode::ota::make_progress_event(s));
        });

        server.on_connect([&](sensornode::UnixServer::ClientId id, const sensornode::PeerCred& p) {
            SN_LOG_INFO("ipc", "client connected cid=%" PRIu64 " pid=%d uid=%d gid=%d", id,
                        static_cast<int>(p.pid), static_cast<int>(p.uid), static_cast<int>(p.gid));
            server.send(id, sensornode::ota::make_hello(ota.snapshot()));
            return true;
        });

        server.on_disconnect([&](sensornode::UnixServer::ClientId id, const char* reason) {
            SN_LOG_INFO("ipc", "client disconnected: %s cid=%" PRIu64, reason, id);
        });

        server.on_line([&](sensornode::UnixServer::ClientId id, const std::string& line) {
            SN_LOG_DEBUG("ipc", "line received cid=%" PRIu64 " line=%s", id,
                         sensornode::log::quote(line).c_str());
            server.send(id, sensornode::ota::handle_command(line, sys, ota));
        });

        // Chưa có việc background nào để kiểm tra, tick chạy được nghĩa là loop không bị kẹt.
        sensornode::Watchdog watchdog(loop, [] { return true; });

        SN_LOG_NOTICE("main", "ready");
        sensornode::systemd::notify("READY=1");
        update_status();
        loop.run();

        loop.remove(signals.getfd());

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        SN_LOG_ERR("main", "fatal: %s", e.what());
        return 1;
    }
}
