#include "common/atomic_file.h"

#include "common/log.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace sensornode {

bool write_file_atomic(const std::string& path, std::string_view text, std::string& error) {
    const std::string tmp = path + ".tmp";

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        error = log::errstr(errno);
        return false;
    }

    const bool written = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()) && ::fsync(fd) == 0;
    const int  err     = errno;
    ::close(fd);

    if (!written || std::rename(tmp.c_str(), path.c_str()) != 0) {
        error = log::errstr(written ? errno : err);
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace sensornode
