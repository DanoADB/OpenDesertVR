// SPDX-License-Identifier: MIT
#include "odv/core/metrics.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace odv {
void TimingWindow::add(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0)
        throw std::invalid_argument("timing must be finite and nonnegative");
    if (values_.size() == capacity)
        values_.pop_front();
    values_.push_back(milliseconds);
    ++total_;
}
Json TimingWindow::summary() const {
    if (values_.empty())
        return {{"status", "NOT_RUN"}, {"samples", 0}, {"total_samples", total_}};
    std::vector<double> sorted(values_.begin(), values_.end());
    std::sort(sorted.begin(), sorted.end());
    // Nearest-rank percentiles: rank=ceil(p*N), with the first element at rank 1.
    const auto percentile = [&](std::size_t percent) { return sorted[(sorted.size() * percent + 99) / 100 - 1]; };
    return {{"status", "PASS"},         {"samples", sorted.size()}, {"total_samples", total_},
            {"median", percentile(50)}, {"p95", percentile(95)},    {"max", sorted.back()}};
}
} // namespace odv
