#include "common/signal_fd.h"

#include <sys/signalfd.h>
#include <unistd.h>

#include <cstdio>
#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace sensornode {
SignalFd::SignalFd(std::initializer_list<int> signals) {
    if (sigemptyset(&mask_) < 0) {
        throw std::system_error(errno, std::generic_category(), "sigemptyset");
    }

    for (const int sig : signals) {
        if (sigaddset(&mask_, sig) < 0) {
            throw std::invalid_argument("SignalFd: invalid signal number");
        }
    }

    // Block các signal trong list. Từ dòng này, SIGINT/SIGTERM không còn kill process nữa,
    // mà pending trong kernel cho tới khi được đọc.
    if (sigprocmask(SIG_BLOCK, &mask_, nullptr) < 0) {
        throw std::system_error(errno, std::generic_category(), "sigprocmask");
    }

    // Tạo fd
    fd_ = signalfd(-1, &mask_, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "signalfd");
    }
}

void SignalFd::drain(const std::function<void(int signo)>& handler) {
    for (;;) {
        signalfd_siginfo info{};
        ssize_t r = read(fd_, &info, sizeof info);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) return;
            throw std::system_error(errno, std::generic_category(), "read(signalfd)");
        }

        if (r != static_cast<ssize_t>(sizeof info)) {
            throw std::system_error(errno, std::generic_category(), "read(signalfd)");
        }

        handler(static_cast<int>(info.ssi_signo));
    }
}

}   // namespace sensornode
