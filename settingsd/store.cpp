#include "settingsd/store.h"

#include "common/atomic_file.h"
#include "common/log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace sensornode::settings {

using nlohmann::json;

namespace {

using namespace std::chrono_literals;

constexpr auto kSaveDelay = 2s;

struct Spec {
    const char*         key;
    json                fallback;
    std::pair<int, int> range;
};

const Spec kSchema[] = {
    {"brightness", 72,    {0, 100}},
    {"auto_mode",  false, {}},
};

const Spec* find_spec(const std::string& key) {
    for (const Spec& s : kSchema) {
        if (key == s.key) return &s;
    }
    return nullptr;
}

bool valid(const Spec& spec, const json& v) {
    if (spec.fallback.is_boolean()) return v.is_boolean();
    if (!v.is_number_integer()) return false;
    const auto n = v.get<long long>();
    return n >= spec.range.first && n <= spec.range.second;
}

json read_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return json(nullptr);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const json doc = json::parse(text, nullptr, false);
    return !doc.is_discarded() && doc.is_object() ? doc : json(nullptr);
}

} // namespace

Store::Store(EventLoop& loop, std::string path)
    : path_(std::move(path)),
      save_timer_(loop, [this](std::uint64_t) { flush(); }) {
    load();
}

void Store::load() {
    for (const Spec& s : kSchema) values_[s.key] = s.fallback;

    if (!std::filesystem::exists(path_)) return;

    root_ = read_json(path_);
    if (root_.is_null()) {
        SN_LOG_WARN("settings", "corrupt settings, using defaults path=%s", path_.c_str());
        root_ = json::object();
        return;
    }

    for (const Spec& s : kSchema) {
        const auto it = root_.find(s.key);
        if (it == root_.end()) continue;
        if (valid(s, *it)) {
            values_[s.key] = *it;
        } else {
            SN_LOG_WARN("settings", "invalid value ignored key=%s", s.key);
        }
    }
}

const char* Store::set(const std::string& key, const json& value) {
    const Spec* spec = find_spec(key);
    if (!spec) return "unknown key";
    if (!valid(*spec, value)) return "invalid value";

    if (values_[key] != value) {
        values_[key] = value;
        dirty_ = true;
        save_timer_.start_oneshot(kSaveDelay);
    }
    return nullptr;
}

void Store::flush() {
    if (dirty_ && save()) dirty_ = false;
}

bool Store::save() {
    root_.update(values_);

    std::string err;
    if (!write_file_atomic(path_, root_.dump(2) + "\n", err)) {
        SN_LOG_ERR("settings", "save failed: %s path=%s", err.c_str(), path_.c_str());
        return false;
    }
    return true;
}

} // namespace sensornode::settings
