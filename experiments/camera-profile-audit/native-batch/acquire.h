// SPDX-License-Identifier: MIT
#pragma once
#include "selected_read.h"
#include <algorithm>
#include <cstring>

namespace odv::research {
// Reader supplies bounded reads. The same algorithm is exercised with an owned
// producer that mutates at exact read boundaries, without scheduling assumptions.
template <class Reader>
void acquire(Reader &reader, std::uint64_t global, std::uint64_t table, std::uint32_t offset, Result &result) noexcept {
    result = Result{};
    if (offset < 8 || offset > 4096 || offset % 8 || !global || !table) {
        result.status = Status::invalid_arguments;
        return;
    }
    auto read = [&](std::uint64_t address, void *destination, std::uint32_t size, std::uint32_t alignment) {
        if (reader.expired()) {
            result.status = Status::deadline_expired;
            return false;
        }
        ++result.read_calls;
        if (!reader.read(address, destination, size, alignment)) {
            result.status = Status::read_rejected;
            return false;
        }
        result.payload_bytes += size;
        return true;
    };
    std::uint64_t candidate{}, first_table{}, source{};
    if (!read(global, &candidate, 8, 8))
        return;
    if (!candidate) {
        result.status = Status::association_unavailable;
        return;
    }
    if (!read(candidate, &first_table, 8, 8))
        return;
    if (first_table != table) {
        result.status = Status::association_unavailable;
        return;
    }
    // Prevent overflow before computing the published source-pointer member.
    if (candidate > UINT64_MAX - offset) {
        result.status = Status::invalid_arguments;
        return;
    }
    if (!read(candidate + offset, &source, 8, 8))
        return;
    if (!source) {
        result.status = Status::source_unavailable;
        return;
    }
    if (source > UINT64_MAX - 0xac4) {
        result.status = Status::invalid_arguments;
        return;
    }
    result.candidate = candidate;
    result.source = source;
    for (auto *copy : {&result.first, &result.second}) {
        std::size_t start = 0;
        for (auto range : ranges) {
            if (!read(source + range.offset, copy->data() + start, range.size, 4))
                return;
            start += range.size;
        }
    }
    std::uint32_t counter_after{};
    std::uint64_t source_after{}, table_after{}, candidate_after{};
    if (!read(source + 0x20, &counter_after, 4, 4) || !read(candidate + offset, &source_after, 8, 8) ||
        !read(candidate, &table_after, 8, 8) || !read(global, &candidate_after, 8, 8))
        return;
    std::uint32_t counter_before{};
    std::memcpy(&counter_before, result.first.data(), sizeof(counter_before));
    result.checks = (candidate == candidate_after ? 1u : 0u) | (source == source_after ? 2u : 0u) |
                    (table_after == table ? 4u : 0u) | (result.first == result.second ? 8u : 0u) |
                    (counter_after == counter_before ? 16u : 0u);
    std::size_t start = 0;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        if (!std::equal(result.first.begin() + start, result.first.begin() + start + ranges[i].size,
                        result.second.begin() + start))
            result.changed_ranges |= 1u << i;
        start += ranges[i].size;
    }
    result.status = result.checks == all_checks ? Status::observed_equal : Status::coherence_rejected;
}
} // namespace odv::research
