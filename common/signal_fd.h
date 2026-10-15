#pragma once

#include <csignal>
#include <functional>
#include <initializer_list>

namespace sensornode {

class SignalFd
{
public:
    explicit SignalFd(std::initializer_list<int> signals);

    SignalFd(const SignalFd&) = delete;
    SignalFd& operator=(const SignalFd&) = delete;

    int getfd() const noexcept { return fd_; }

    void drain(const std::function<void(int signo)>& handler);

private:
    sigset_t mask_;
    int fd_;
};

} // namespace sensornode

