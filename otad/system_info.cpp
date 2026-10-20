#include "otad/system_info.h"

#include <libuboot.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

namespace sensornode::ota {

namespace {

constexpr const char* kSlotADev = "/dev/mmcblk0p1";
constexpr const char* kSlotBDev = "/dev/mmcblk0p2";

std::string errno_message(int err) {
    return std::strerror(err);
}

std::optional<int> parse_int(const char* s) {
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < 0 || v > 1'000'000) return std::nullopt;
    return static_cast<int>(v);
}

void read_sw_version(const std::string& path, SystemStatus& st) {
    std::ifstream f(path);
    if (!f) {
        st.errors.push_back({"sw-versions", errno_message(errno)});
        return;
    }

    std::string line;
    std::getline(f, line);
    const auto first = line.find_first_not_of(" \t\r");
    const auto last  = line.find_last_not_of(" \t\r");
    if (first == std::string::npos) {
        st.errors.push_back({"sw-versions", "empty file"});
        return;
    }
    st.sw_version = line.substr(first, last - first + 1);
}

void read_running_slot(const std::string& path, SystemStatus& st) {
    std::ifstream f(path);
    if (!f) {
        st.errors.push_back({"cmdline", errno_message(errno)});
        return;
    }

    std::stringstream ss;
    ss << f.rdbuf();

    std::string tok;
    while (ss >> tok) {
        if (tok.rfind("root=", 0) != 0) continue;
        const std::string dev = tok.substr(5);
        if (dev == kSlotADev) {
            st.running_slot = "A";
        } else if (dev == kSlotBDev) {
            st.running_slot = "B";
        } else {
            st.errors.push_back({"cmdline", "root=" + dev + " is not an A/B slot"});
        }
        return;
    }
    st.errors.push_back({"cmdline", "no root= parameter"});
}

struct UbootCtxDeleter {
    void operator()(uboot_ctx* ctx) const { libuboot_exit(ctx); }
};
using UbootCtx = std::unique_ptr<uboot_ctx, UbootCtxDeleter>;

// libuboot_get_env trả về chuỗi malloc, caller phải free.
std::optional<std::string> get_env(uboot_ctx* ctx, const char* name) {
    std::unique_ptr<char, decltype(&std::free)> v(libuboot_get_env(ctx, name), &std::free);
    if (!v) return std::nullopt;
    return std::string(v.get());
}

std::optional<int> get_env_int(uboot_ctx* ctx, const char* name, SystemStatus& st) {
    const auto v = get_env(ctx, name);
    if (!v) return std::nullopt;
    auto n = parse_int(v->c_str());
    if (!n) st.errors.push_back({"bootenv", std::string("invalid ") + name + "='" + *v + "'"});
    return n;
}

void read_boot_env(const std::string& config, SystemStatus& st) {
    uboot_ctx* raw = nullptr;
    if (libuboot_initialize(&raw, nullptr) < 0) {
        st.errors.push_back({"bootenv", "libuboot_initialize failed"});
        return;
    }
    UbootCtx ctx(raw);

    int ret = libuboot_read_config(ctx.get(), config.c_str());
    if (ret < 0) {
        st.errors.push_back({"bootenv", "read config " + config + ": " + errno_message(-ret)});
        return;
    }

    ret = libuboot_open(ctx.get());
    if (ret < 0) {
        st.errors.push_back({"bootenv", "open: " + errno_message(-ret)});
        return;
    }

    st.active_slot = get_env(ctx.get(), "active_slot");
    st.ustate      = get_env_int(ctx.get(), "ustate", st);
    st.boot_count  = get_env_int(ctx.get(), "boot_count", st);
    st.boot_limit  = get_env_int(ctx.get(), "boot_limit", st);

    if (st.active_slot && *st.active_slot != "A" && *st.active_slot != "B") {
        st.errors.push_back({"bootenv", "invalid active_slot='" + *st.active_slot + "'"});
        st.active_slot.reset();
    }

    libuboot_close(ctx.get());
}

} // namespace

SystemStatus SystemInfo::read() const {
    SystemStatus st;
    read_sw_version(paths_.sw_versions, st);
    read_running_slot(paths_.cmdline, st);
    read_boot_env(paths_.fw_env_config, st);
    return st;
}

} // namespace sensornode::ota
