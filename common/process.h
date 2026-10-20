#pragma once

#include <sys/types.h>

#include <functional>
#include <string>
#include <vector>

namespace sensornode {

class EventLoop;

class Process {
public:
    struct ExitStatus {
        bool exited = false;    // false nghĩa là bị signal kill
        int  code   = 0;        // exit code khi exited, số signal khi không
        bool success() const noexcept { return exited && code == 0; }
        std::string describe() const;
    };

    using OutputHandler = std::function<void(const std::string& line)>;
    using ExitHandler   = std::function<void(const ExitStatus&)>;

    explicit Process(EventLoop& loop);
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    void start(const std::vector<std::string>& argv);

    // Gửi signal cho cả process group, gồm cả process con mà child tự spawn.
    void kill(int signo);

    bool  running() const noexcept { return pid_ > 0; }
    pid_t pid() const noexcept { return pid_; }

    void on_output(OutputHandler h) { on_output_ = std::move(h); }
    void on_exit(ExitHandler h)     { on_exit_ = std::move(h); }

private:
    void on_pidfd_ready();
    void read_output();
    void flush_partial();
    void close_output();
    void reap(ExitStatus& st);

    EventLoop&    loop_;
    pid_t         pid_    = -1;
    int           pidfd_  = -1;
    int           out_fd_ = -1;
    std::string   partial_;
    OutputHandler on_output_;
    ExitHandler   on_exit_;
};

} // namespace sensornode
