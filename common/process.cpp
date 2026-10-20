#include "common/process.h"
#include "common/event_loop.h"
#include "common/log.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <system_error>

extern char** environ;

namespace sensornode {

namespace {

constexpr auto kPidfd = static_cast<idtype_t>(3);

constexpr std::size_t kReadChunk = 4096;
constexpr std::size_t kMaxLine   = 1024;

int pidfd_open(pid_t pid) {
    return static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
}

[[noreturn]] void throw_errno(int err, const std::string& what) {
    throw std::system_error(err, std::generic_category(), what);
}

struct UniqueFd {
    int fd = -1;
    ~UniqueFd() { if (fd >= 0) ::close(fd); }
    int release() { const int f = fd; fd = -1; return f; }
};

struct SpawnAttr {
    posix_spawnattr_t a;
    SpawnAttr()  { posix_spawnattr_init(&a); }
    ~SpawnAttr() { posix_spawnattr_destroy(&a); }
};

struct SpawnActions {
    posix_spawn_file_actions_t a;
    SpawnActions()  { posix_spawn_file_actions_init(&a); }
    ~SpawnActions() { posix_spawn_file_actions_destroy(&a); }
};

} // namespace

std::string Process::ExitStatus::describe() const {
    if (exited) return "exit code " + std::to_string(code);
    const char* name = sigabbrev_np(code);
    return name ? std::string("killed by SIG") + name : "killed by signal " + std::to_string(code);
}

Process::Process(EventLoop& loop) : loop_(loop) {}

Process::~Process() {
    close_output();
    if (!running()) return;

    SN_LOG_WARN("proc", "killing child pid=%d", static_cast<int>(pid_));
    ::kill(-pid_, SIGKILL);
    ExitStatus st;
    reap(st);
    loop_.remove(pidfd_);
    ::close(pidfd_);
}

void Process::start(const std::vector<std::string>& argv) {
    if (running()) throw std::logic_error("Process::start: already running");
    if (argv.empty()) throw std::invalid_argument("Process::start: empty argv");

    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) throw_errno(errno, "pipe2");
    UniqueFd rd{p[0]};
    UniqueFd wr{p[1]};
    if (fcntl(rd.fd, F_SETFL, O_NONBLOCK) < 0) throw_errno(errno, "fcntl(O_NONBLOCK)");

    SpawnActions fa;
    posix_spawn_file_actions_addopen(&fa.a, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa.a, wr.fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa.a, wr.fd, STDERR_FILENO);

    sigset_t none;
    sigset_t all;
    sigemptyset(&none);
    sigfillset(&all);

    SpawnAttr attr;
    posix_spawnattr_setsigmask(&attr.a, &none);
    posix_spawnattr_setsigdefault(&attr.a, &all);
    posix_spawnattr_setpgroup(&attr.a, 0);
    posix_spawnattr_setflags(&attr.a, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF |
                                      POSIX_SPAWN_SETPGROUP);

    std::vector<char*> args;
    for (const auto& s : argv) args.push_back(const_cast<char*>(s.c_str()));
    args.push_back(nullptr);

    pid_t pid = -1;
    const int err = posix_spawnp(&pid, args[0], &fa.a, &attr.a, args.data(), environ);
    if (err != 0) throw_errno(err, "spawn " + argv[0]);

    ::close(wr.release());

    const int pidfd = pidfd_open(pid);
    if (pidfd < 0) {
        const int e = errno;
        ::kill(-pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        throw_errno(e, "pidfd_open");
    }

    pid_    = pid;
    pidfd_  = pidfd;
    out_fd_ = rd.release();
    partial_.clear();

    loop_.add(out_fd_, EPOLLIN, [this](std::uint32_t) { read_output(); });
    loop_.add(pidfd_, EPOLLIN, [this](std::uint32_t) { on_pidfd_ready(); });
}

void Process::kill(int signo) {
    if (running()) ::kill(-pid_, signo);
}

void Process::read_output() {
    char buf[kReadChunk];

    while (out_fd_ >= 0) {
        const ssize_t n = ::read(out_fd_, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) return;
            SN_LOG_WARN("proc", "read output failed: %s pid=%d", log::errstr(errno).c_str(),
                        static_cast<int>(pid_));
            close_output();
            return;
        }

        if (n == 0) {
            flush_partial();
            close_output();
            return;
        }

        partial_.append(buf, static_cast<std::size_t>(n));
        emit_lines();
    }
}

// Dòng dài hơn kMaxLine bị cắt thành nhiều dòng, dù '\n' đến cùng lần read hay lần sau.
void Process::emit_lines() {
    for (;;) {
        std::string line;
        const std::size_t pos = partial_.find('\n');
        if (pos != std::string::npos && pos <= kMaxLine) {
            line = partial_.substr(0, pos);
            partial_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
        } else if (partial_.size() >= kMaxLine) {
            line = partial_.substr(0, kMaxLine);
            partial_.erase(0, kMaxLine);
        } else {
            return;
        }
        if (on_output_) on_output_(line);
    }
}

void Process::flush_partial() {
    if (partial_.empty()) return;
    std::string line;
    line.swap(partial_);
    if (on_output_) on_output_(line);
}

void Process::close_output() {
    if (out_fd_ < 0) return;
    loop_.remove(out_fd_);
    ::close(out_fd_);
    out_fd_ = -1;
}

void Process::reap(ExitStatus& st) {
    siginfo_t info{};
    while (waitid(kPidfd, static_cast<id_t>(pidfd_), &info, WEXITED) < 0) {
        if (errno == EINTR) continue;
        SN_LOG_ERR("proc", "waitid failed: %s pid=%d", log::errstr(errno).c_str(),
                   static_cast<int>(pid_));
        st.exited = true;
        st.code   = -1;
        return;
    }

    st.exited = info.si_code == CLD_EXITED;
    st.code   = info.si_status;
}

void Process::on_pidfd_ready() {
    read_output();
    flush_partial();
    close_output();

    ::kill(-pid_, SIGKILL);

    ExitStatus st;
    reap(st);

    loop_.remove(pidfd_);
    ::close(pidfd_);
    pidfd_ = -1;
    pid_   = -1;

    if (on_exit_) on_exit_(st);
}

} // namespace sensornode
