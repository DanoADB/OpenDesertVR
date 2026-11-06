// SPDX-License-Identifier: MIT
// Selected layout hypotheses adapted from CrimsonDesertTelemetry 434b3b9,
// Copyright (c) 2026 CrimsonDesertTelemetry contributors; see the full MIT
// notice at docs/licenses/CrimsonDesertTelemetry-MIT.txt.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#ifdef ODV_SELECTED_READ_EXPORTS
#define ODV_READ_API __declspec(dllexport)
#else
#define ODV_READ_API __declspec(dllimport)
#endif

namespace odv::research {
struct Range {
    std::uint32_t offset, size;
};
// Same selected historical hypotheses as camera_fields.py; see the pinned MIT
// source notice in docs/licenses/CrimsonDesertTelemetry-MIT.txt. No whole buffer.
inline constexpr std::array<Range, 8> ranges{
    {{0x20, 4}, {0x30, 16}, {0x80, 12}, {0x90, 12}, {0x3e0, 128}, {0x4e0, 64}, {0x860, 4}, {0xac0, 4}}};
inline constexpr std::uint32_t copy_size = 244;
static_assert([] {
    std::uint32_t size = 0;
    for (auto range : ranges)
        size += range.size;
    return size;
}() == copy_size);
inline constexpr std::uint32_t all_checks = 31;
enum class Status : std::uint32_t {
    not_run,
    observed_equal,
    invalid_arguments,
    association_unavailable,
    source_unavailable,
    read_rejected,
    coherence_rejected,
    deadline_expired
};
// Versioned, fixed-size local ABI. Raw copies/chain identities stay in observer
// RAM; consumers must not serialize them as evidence or claim atomicity.
struct Result {
    std::uint32_t version{1};
    Status status{Status::not_run};
    std::uint32_t checks{}, changed_ranges{}, read_calls{}, payload_bytes{};
    std::uint64_t elapsed_microseconds{}, candidate{}, source{};
    std::array<std::byte, copy_size> first{}, second{};
};
static_assert(sizeof(Result) == 536);
} // namespace odv::research

// Borrow a caller-owned SYNCHRONIZE/QUERY_INFORMATION/VM_READ handle. Does not
// open a process, close the handle, change privileges/protections or retry.
extern "C" ODV_READ_API void odv_read_selected_v1(void *process, std::uint64_t global, std::uint64_t table,
                                                  std::uint32_t source_offset, odv::research::Result *result) noexcept;
