/* 
 * Logger dùng chung cho các daemon.
 *
 * Cấu trúc dòng:
 *   <prefix><component>: <sự kiện>[: <nguyên nhân>] [key=value ...] [src=<file>:<line>]
 *
 * Prefix phụ thuộc stderr đang nối tới đâu:
 *   terminal : 2026-10-19 10:02:11.482 [  123.456] W mqtt: connect failed: ... src=mqtt_client.cpp:142
 *   journald : <4>mqtt: connect failed: ... src=mqtt_client.cpp:142
 * 
 */

#include "common/log.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace sensornode::log {

namespace {

constexpr std::size_t kMaxLine = 1024;

struct State {
    Level max     = Level::info;
    bool  journal = false;
};

State g_state;

class LineBuffer {
public:
    void append(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void vappend(const char* fmt, va_list ap) __attribute__((format(printf, 2, 0)));
    void sanitize_from(std::size_t pos);
    void finish(const char* suffix);

    std::size_t size() const { return len_; }
    const char* data() const { return buf_; }

private:
    static constexpr std::size_t kSuffixRoom = 128;
    static constexpr std::size_t kCapacity   = kMaxLine - kSuffixRoom;

    char        buf_[kMaxLine];
    std::size_t len_       = 0;
    bool        truncated_ = false;
};

void LineBuffer::append(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
}

void LineBuffer::vappend(const char* fmt, va_list ap) {
    if (truncated_) return;

    const std::size_t room = kCapacity - len_;
    const int n = std::vsnprintf(buf_ + len_, room + 1, fmt, ap);
    if (n < 0) return;

    // vsnprintf trả về độ dài lẽ ra cần, không phải số byte đã ghi.
    if (static_cast<std::size_t>(n) > room) {
        len_       = kCapacity;
        truncated_ = true;
    } else {
        len_ += static_cast<std::size_t>(n);
    }
}

void LineBuffer::sanitize_from(std::size_t pos) {
    for (std::size_t i = pos; i < len_; ++i) {
        const auto c = static_cast<unsigned char>(buf_[i]);
        if (c < 0x20 || c == 0x7f) buf_[i] = '?';
    }
}

void LineBuffer::finish(const char* suffix) {
    const auto put = [this](const char* s, std::size_t max) {
        const std::size_t n = std::min(std::strlen(s), max);
        std::memcpy(buf_ + len_, s, n);
        len_ += n;
    };

    if (truncated_) put("...", 3);
    put(suffix, kSuffixRoom - 3 - 1);
    buf_[len_++] = '\n';
}

char level_letter(Level lv) {
    switch (lv) {
    case Level::err:     return 'E';
    case Level::warning: return 'W';
    case Level::notice:  return 'N';
    case Level::info:    return 'I';
    case Level::debug:   return 'D';
    }
    return '?';
}

bool shows_source(Level lv) {
    return lv == Level::err || lv == Level::warning || lv == Level::debug;
}

const char* basename_of(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool stderr_is_journal() {
    const char* env = std::getenv("JOURNAL_STREAM");
    if (!env) return false;

    unsigned long long dev = 0;
    unsigned long long ino = 0;
    if (std::sscanf(env, "%llu:%llu", &dev, &ino) != 2) return false;

    struct stat st{};
    if (fstat(STDERR_FILENO, &st) < 0) return false;
    return st.st_dev == dev && st.st_ino == ino;
}

void append_timestamp(LineBuffer& b) {
    timespec wall{};
    timespec mono{};
    clock_gettime(CLOCK_REALTIME, &wall);
    clock_gettime(CLOCK_MONOTONIC, &mono);

    tm t{};
    localtime_r(&wall.tv_sec, &t);

    b.append("%04d-%02d-%02d %02d:%02d:%02d.%03ld [%6lld.%03ld] ",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
             wall.tv_nsec / 1000000,
             static_cast<long long>(mono.tv_sec), mono.tv_nsec / 1000000);
}

void append_prefix(LineBuffer& b, Level lv) {
    if (g_state.journal) {
        b.append("<%d>", static_cast<int>(lv));
        return;
    }
    append_timestamp(b);
    b.append("%c ", level_letter(lv));
}

void write_all(int fd, const char* p, std::size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
}

} // namespace

void init() {
    g_state.journal = stderr_is_journal();

    const char* env = std::getenv("SENSORNODE_LOG_LEVEL");
    if (!env) return;

    Level lv;
    if (parse_level(env, lv)) {
        g_state.max = lv;
    } else {
        SN_LOG_WARN("log", "invalid level ignored env=SENSORNODE_LOG_LEVEL value=%s using=%s",
                    quote(env).c_str(), level_name(g_state.max));
    }
}

void set_level(Level max) { g_state.max = max; }

Level level() { return g_state.max; }

bool enabled(Level lv) {
    return static_cast<int>(lv) <= static_cast<int>(g_state.max);
}

const char* level_name(Level lv) {
    switch (lv) {
    case Level::err:     return "err";
    case Level::warning: return "warning";
    case Level::notice:  return "notice";
    case Level::info:    return "info";
    case Level::debug:   return "debug";
    }
    return "unknown";
}

bool parse_level(std::string_view name, Level& out) {
    for (const Level lv : {Level::err, Level::warning, Level::notice, Level::info, Level::debug}) {
        if (name == level_name(lv)) {
            out = lv;
            return true;
        }
    }
    return false;
}

void write(Level lv, const char* component, const char* file, int line, const char* fmt, ...) {
    const int saved_errno = errno;

    LineBuffer b;
    append_prefix(b, lv);
    b.append("%s: ", component);

    const std::size_t body = b.size();
    va_list ap;
    va_start(ap, fmt);
    b.vappend(fmt, ap);
    va_end(ap);
    b.sanitize_from(body);

    char src[96] = "";
    if (shows_source(lv)) {
        std::snprintf(src, sizeof src, " src=%s:%d", basename_of(file), line);
    }
    b.finish(src);

    write_all(STDERR_FILENO, b.data(), b.size());
    errno = saved_errno;
}


// Biến một chuỗi lấy từ bên ngoài thành một giá trị nằm gọn trong dấu ngoặc kép
// để chuỗi đó không thể phá vỡ cấu trúc dòng log.
std::string quote(std::string_view s, std::size_t max_len) {
    const bool cut = s.size() > max_len;
    if (cut) s = s.substr(0, max_len);

    std::string out;
    out.reserve(s.size() + 8);
    out += '"';
    for (const char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default: {
            const auto uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || uc == 0x7f) {
                char hex[5];
                std::snprintf(hex, sizeof hex, "\\x%02x", uc);
                out += hex;
            } else {
                out += c;
            }
        }
        }
    }
    if (cut) out += "...";
    out += '"';
    return out;
}

std::string errstr(int err) {
    std::string out = std::strerror(err);
    out += " (";

#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 32))
    const char* name = strerrorname_np(err);
    out += name ? name : "errno " + std::to_string(err);
#else
    out += "errno " + std::to_string(err);
#endif
    out += ')';
    return out;
}

} // namespace sensornode::log
