#include "telemetryd/drivers/i2c_dev.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <system_error>

namespace sensornode {

I2cBus::I2cBus(const std::string& path) {
    fd_ = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + path);
    }

    unsigned long funcs = 0;
    if (ioctl(fd_, I2C_FUNCS, &funcs) < 0) {
        const int err = errno;
        close(fd_);
        throw std::system_error(err, std::generic_category(), "I2C_FUNCS " + path);
    }
    if (!(funcs & I2C_FUNC_I2C)) {
        close(fd_);
        throw std::runtime_error(path + ": adapter does not support plain I2C (I2C_RDWR)");
    }
}

I2cBus::~I2cBus() {
    if (fd_ >= 0) close(fd_);
}

int I2cBus::write(std::uint16_t addr, const std::uint8_t* data, std::size_t len) {
    return transfer(addr, 0, const_cast<std::uint8_t*>(data), len);
}

int I2cBus::read(std::uint16_t addr, std::uint8_t* buf, std::size_t len) {
    return transfer(addr, I2C_M_RD, buf, len);
}

// Gửi 1 message I2C xuống kernel: START → địa chỉ → dữ liệu → STOP.
int I2cBus::transfer(std::uint16_t addr, std::uint16_t flags, std::uint8_t* buf, std::size_t len) {
    if (len > UINT16_MAX) return -EINVAL;

    i2c_msg msg{};
    msg.addr  = addr;
    msg.flags = flags;                      // 0 = ghi, I2C_M_RD = đọc
    msg.len   = static_cast<std::uint16_t>(len);
    msg.buf   = buf;

    i2c_rdwr_ioctl_data request{};
    request.msgs  = &msg;
    request.nmsgs = 1;

    if (ioctl(fd_, I2C_RDWR, &request) < 0) return -errno;
    return 0;
}

} // namespace sensornode
