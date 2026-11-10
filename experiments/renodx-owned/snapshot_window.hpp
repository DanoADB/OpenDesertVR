// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace odv::research {
// Caller serializes access. Ordinals describe eligible recording callbacks,
// not GPU execution order. Separate windows are separate observations.
class SnapshotWindow final {
    std::uint32_t first_ = 0, seen_ = 0, empty_intervals_ = 0;
    bool valid_ = false;

  public:
    static constexpr std::uint32_t maximum_first = 4096, capacity = 128;
    static std::optional<std::uint32_t> parse(std::string_view text) noexcept {
        if (text.size() < 2 || text.size() > 5 || text.back() != '\n')
            return {};
        text.remove_suffix(1);
        if (text.size() > 1 && text.front() == '0')
            return {};
        std::uint32_t value = 0;
        for (const auto c : text) {
            if (c < '0' || c > '9')
                return {};
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        return value <= maximum_first ? std::optional{value} : std::nullopt;
    }
    void begin(std::optional<std::uint32_t> first) noexcept {
        valid_ = first && *first <= maximum_first;
        first_ = valid_ ? *first : 0;
        seen_ = 0;
        empty_intervals_ = 0;
    }
    std::optional<std::uint32_t> take() noexcept {
        if (!valid_)
            return {};
        const auto ordinal = seen_;
        if (seen_ <= first_ + capacity)
            ++seen_; // Saturate after the first record beyond the window.
        if (ordinal < first_ || ordinal >= first_ + capacity)
            return {};
        return ordinal;
    }
    bool valid() const noexcept { return valid_; }
    // Multiple Present callbacks may precede any eligible recording activity.
    // Wait at most four empty intervals; never combine two active intervals.
    bool finish_interval() noexcept {
        return !valid_ || seen_ != 0 || ++empty_intervals_ >= 4;
    }
    bool truncated() const noexcept { return valid_ && seen_ > first_ + capacity; }
    std::uint32_t first() const noexcept { return first_; }
    std::uint32_t seen() const noexcept { return seen_; }
};
} // namespace odv::research
