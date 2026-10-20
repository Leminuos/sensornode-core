#pragma once

#include <string>
#include <string_view>

namespace sensornode::log {

enum class Level {
    err     = 3,
    warning = 4,
    notice  = 5,
    info    = 6,
    debug   = 7,
};

void init();

void  set_level(Level max);
Level level();
bool  enabled(Level lv);

const char* level_name(Level lv);
bool        parse_level(std::string_view name, Level& out);

void write(Level lv, const char* component, const char* file, int line,
           const char* fmt, ...) __attribute__((format(printf, 5, 6)));

std::string quote(std::string_view s, std::size_t max_len = 200);

// Input/output error (EIO)
std::string errstr(int err);

} // namespace sensornode::log

#define SN_LOG_AT(lv, component, ...)                                             \
    do {                                                                          \
        if (::sensornode::log::enabled(lv))                                       \
            ::sensornode::log::write(lv, component, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define SN_LOG_ERR(component, ...)    SN_LOG_AT(::sensornode::log::Level::err,     component, __VA_ARGS__)
#define SN_LOG_WARN(component, ...)   SN_LOG_AT(::sensornode::log::Level::warning, component, __VA_ARGS__)
#define SN_LOG_NOTICE(component, ...) SN_LOG_AT(::sensornode::log::Level::notice,  component, __VA_ARGS__)
#define SN_LOG_INFO(component, ...)   SN_LOG_AT(::sensornode::log::Level::info,    component, __VA_ARGS__)
#define SN_LOG_DEBUG(component, ...)  SN_LOG_AT(::sensornode::log::Level::debug,   component, __VA_ARGS__)
