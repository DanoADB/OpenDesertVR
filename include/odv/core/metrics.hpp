// SPDX-License-Identifier: MIT
#pragma once
#include "odv/core/core.hpp"
#include <cstddef>
#include <cstdint>

namespace odv {
// Bounded rolling window; lifetime sample count makes truncation explicit.
class TimingWindow {
    std::deque<double> values_;
    std::uint64_t total_{};

  public:
    static constexpr std::size_t capacity = 4096;
    void add(double milliseconds);
    bool empty() const { return values_.empty(); }
    Json summary() const;
};
} // namespace odv
