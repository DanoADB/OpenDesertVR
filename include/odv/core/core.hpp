// SPDX-License-Identifier: MIT
#pragma once
#include <deque>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <variant>

namespace odv {
using Json = nlohmann::json;
enum class ErrorCode { configuration, runtime, headset, extension, adapter, allocation, device, session };
struct Error {
    ErrorCode code;
    std::string message;
};
template <class T> using Result = std::variant<T, Error>;
class Failure : public std::runtime_error {
  public:
    ErrorCode code;
    Failure(ErrorCode c, const std::string &message) : std::runtime_error(message), code(c) {}
};
struct Config {
    double near_z = 0.05, far_z = 100;
    int duration_seconds = 60;
    bool debug_layer = false;
};
Result<Config> parse_config(const std::string &text);
Config load_config(const std::string &path);
// Events accept stable codes and numeric fields only. Never log external messages,
// runtime paths, device IDs, usernames or exception text to exported reports.
class Log {
    std::deque<Json> events_;

  public:
    void record(const std::string &code, const Json &numeric_fields = Json::object());
    Json snapshot() const;
};
Json check(const std::string &name, const std::string &status, const std::string &detail);
Json empty_report();
} // namespace odv
