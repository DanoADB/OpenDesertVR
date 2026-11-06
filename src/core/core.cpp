// SPDX-License-Identifier: MIT
#include "odv/core/core.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace odv {
Result<Config> parse_config(const std::string &text) {
    try {
        if (text.size() > 65536)
            throw Failure(ErrorCode::configuration, "configuration exceeds 64 KiB");
        // Reject duplicate keys rather than silently applying the last value.
        std::vector<std::string> keys;
        auto j = Json::parse(text, [&keys](int, Json::parse_event_t event, Json &value) {
            if (event == Json::parse_event_t::key) {
                auto key = value.get<std::string>();
                if (std::find(keys.begin(), keys.end(), key) != keys.end())
                    throw Failure(ErrorCode::configuration, "duplicate configuration key");
                keys.push_back(key);
            }
            return true;
        });
        if (!j.is_object() || !j.contains("schema_version") || !j["schema_version"].is_number_integer() ||
            j["schema_version"] != 1)
            throw Failure(ErrorCode::configuration, "schema_version must be integer 1");
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.key() != "schema_version" && it.key() != "near_z" && it.key() != "far_z" &&
                it.key() != "duration_seconds" && it.key() != "debug_layer")
                throw Failure(ErrorCode::configuration, "unknown configuration key");
        Config c;
        for (auto key : {"near_z", "far_z"})
            if (j.contains(key) && !j[key].is_number())
                throw Failure(ErrorCode::configuration, "clipping planes must be numbers");
        if (j.contains("near_z"))
            c.near_z = j["near_z"].get<double>();
        if (j.contains("far_z"))
            c.far_z = j["far_z"].get<double>();
        if (j.contains("duration_seconds")) {
            if (!j["duration_seconds"].is_number_integer() || j["duration_seconds"] < 1 || j["duration_seconds"] > 3600)
                throw Failure(ErrorCode::configuration, "duration_seconds must be integer 1..3600");
            c.duration_seconds = j["duration_seconds"].get<int>();
        }
        if (j.contains("debug_layer")) {
            if (!j["debug_layer"].is_boolean())
                throw Failure(ErrorCode::configuration, "debug_layer must be boolean");
            c.debug_layer = j["debug_layer"].get<bool>();
        }
        if (!std::isfinite(c.near_z) || !std::isfinite(c.far_z) || c.near_z < 0.01 || c.far_z <= c.near_z ||
            c.far_z > 1000)
            throw Failure(ErrorCode::configuration, "invalid clipping planes");
        return c;
    } catch (const std::exception &) {
        return Error{ErrorCode::configuration, "invalid versioned configuration"};
    }
}
Config load_config(const std::string &path) {
    if (path.empty())
        return {};
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw Failure(ErrorCode::configuration, "cannot open configuration");
    std::string text(65537, '\0');
    f.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(f.gcount()));
    auto result = parse_config(text);
    if (auto e = std::get_if<Error>(&result))
        throw Failure(e->code, e->message);
    return std::get<Config>(result);
}
void Log::record(const std::string &code, const Json &fields) {
    if (code.size() > 64 || code.empty() ||
        !std::all_of(code.begin(), code.end(), [](unsigned char c) { return std::isupper(c) || c == '_'; }))
        throw std::invalid_argument("log code must be a stable uppercase token");
    Json safe = Json::object();
    if (!fields.is_object())
        throw std::invalid_argument("log fields must be an object");
    for (auto it = fields.begin(); it != fields.end(); ++it)
        if (safe.size() < 24 && (it.value().is_number() || it.value().is_boolean()) && it.key().size() <= 32 &&
            std::all_of(it.key().begin(), it.key().end(), [](unsigned char c) { return std::islower(c) || c == '_'; }))
            safe[it.key()] = it.value();
    if (events_.size() == 256)
        events_.pop_front();
    events_.push_back({{"code", code}, {"fields", safe}});
}
Json Log::snapshot() const { return Json(events_); }
Json check(const std::string &name, const std::string &status, const std::string &detail) {
    return {{"name", name}, {"status", status}, {"detail", detail}};
}
Json empty_report() {
    return {{"schema_version", 1},     {"application", "OpenDesertVR 0.1.0"}, {"os", "Windows x64"},
            {"checks", Json::array()}, {"headset_validation", "NOT_RUN"},     {"game_validation", "NOT_RUN"}};
}
} // namespace odv
