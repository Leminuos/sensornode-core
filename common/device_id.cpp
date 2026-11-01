#include "common/device_id.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace sensornode {

namespace fs = std::filesystem;

namespace {

std::string read_mac(const fs::path& iface) {
    std::ifstream in(iface / "address");
    std::string addr;
    in >> addr;

    const bool all_zero = addr.find_first_not_of("0:") == std::string::npos;
    return addr.size() == 17 && !all_zero ? addr : std::string();
}

} // namespace

std::string primary_mac(const std::string& net_dir) {
    std::error_code ec;
    std::vector<fs::path> ifaces;
    for (const auto& entry : fs::directory_iterator(net_dir, ec)) {
        // Interface ảo (lo, sit0, veth...) không có symlink device.
        if (fs::exists(entry.path() / "device", ec)) ifaces.push_back(entry.path());
    }
    std::sort(ifaces.begin(), ifaces.end());

    for (const auto& iface : ifaces) {
        std::string mac = read_mac(iface);
        if (!mac.empty()) return mac;
    }
    return {};
}

std::string device_id_from_mac(const std::string& net_dir) {
    std::string id = primary_mac(net_dir);
    id.erase(std::remove(id.begin(), id.end(), ':'), id.end());
    return id;
}

std::string default_device_id() {
    std::string id = device_id_from_mac();
    if (!id.empty()) return id;

    char host[256] = {};
    if (gethostname(host, sizeof host - 1) != 0) return {};
    std::fprintf(stderr, "warning: no network interface MAC, using hostname '%s' as device id\n", host);
    return host;
}

} // namespace sensornode
