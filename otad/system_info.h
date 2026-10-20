#pragma once

#include <optional>
#include <string>
#include <vector>

namespace sensornode::ota {

struct SystemStatus {
    struct Error {
        const char* source;
        std::string message;
    };

    std::optional<std::string> sw_version;
    std::optional<std::string> running_slot;    // slot kernel đang mount làm rootfs
    std::optional<std::string> active_slot;     // slot U-Boot sẽ boot lần tới
    std::optional<int>         ustate;
    std::optional<int>         boot_count;
    std::optional<int>         boot_limit;
    std::vector<Error>         errors;
};

class SystemInfo {
public:
    struct Paths {
        std::string sw_versions   = "/etc/sw-versions";
        std::string fw_env_config = "/etc/fw_env.config";
        std::string cmdline       = "/proc/cmdline";
    };

    explicit SystemInfo(Paths paths) : paths_(std::move(paths)) {}

    // Đọc lại từ đầu mỗi lần gọi vì SWUpdate, U-Boot, ota-confirm-boot đều có thể đổi env.
    SystemStatus read() const;

    const Paths& paths() const noexcept { return paths_; }

private:
    Paths paths_;
};

} // namespace sensornode::ota
