// SPDX-License-Identifier: MIT
#include "acquire.h"
#include <iostream>

using namespace odv::research;
namespace {
struct OwnedProducer {
    std::uint32_t calls{}, bytes{}, fail_at{}, expire_at{}, mutate_at{}, mutation{};
    std::uint64_t root{0x20000}, table{0x30000}, source{0x40000};
    std::array<std::byte, 0xb00> data{};
    bool expired() const noexcept { return expire_at && calls >= expire_at; }
    bool read(std::uint64_t address, void *output, std::uint32_t size, std::uint32_t alignment) noexcept {
        ++calls;
        if (fail_at == calls || !size || size > 128 || address % alignment)
            return false;
        if (mutate_at == calls) {
            if (mutation == 1)
                data[0x20] = std::byte{1};
            if (mutation == 2)
                data[0x80] = std::byte{1};
            if (mutation == 3)
                root += 8;
            if (mutation == 4)
                source += 8;
            if (mutation == 5)
                table += 8;
            if (mutation >= 6 && mutation < 6 + ranges.size())
                data[ranges[mutation - 6].offset] = std::byte{1};
        }
        const void *input{};
        if (address == 0x10000 && size == 8)
            input = &root;
        if (address == 0x20000 && size == 8)
            input = &table;
        if (address == 0x20000 + 1072 && size == 8)
            input = &source;
        if (address >= 0x40000 && address + size <= 0x40b00)
            input = data.data() + (address - 0x40000);
        if (!input)
            return false;
        std::memcpy(output, input, size);
        bytes += size;
        return true;
    }
};
bool require(bool condition, const char *name) {
    if (!condition)
        std::cerr << "FAIL: " << name << '\n';
    return condition;
}
} // namespace

int main() {
    bool okay = true;
    Result result;
    OwnedProducer stable;
    acquire(stable, 0x10000, 0x30000, 1072, result);
    okay &= require(result.status == Status::observed_equal && result.checks == 31 && result.changed_ranges == 0 &&
                        result.read_calls == 23 && result.payload_bytes == 540 && stable.bytes == 540,
                    "stable owned sequence and bounds");
    // Failure at every read must stop at that read, with no fallback/retry.
    for (std::uint32_t call = 1; call <= 23; ++call) {
        OwnedProducer failed;
        failed.fail_at = call;
        acquire(failed, 0x10000, 0x30000, 1072, result);
        okay &= require(result.status == Status::read_rejected && result.read_calls == call && failed.calls == call &&
                            result.payload_bytes == failed.bytes,
                        "read failure is terminal");
    }
    for (std::uint32_t mutation = 1; mutation <= 5; ++mutation) {
        OwnedProducer torn;
        torn.mutate_at = 12; // Start of second selected copy.
        torn.mutation = mutation;
        acquire(torn, 0x10000, 0x30000, 1072, result);
        std::uint32_t missing = mutation == 1 ? 24 : mutation == 2 ? 8 : mutation == 3 ? 1 : mutation == 4 ? 2 : 4;
        std::uint32_t changed = mutation == 1 ? 1 : mutation == 2 ? 4 : 0;
        okay &= require(result.status == Status::coherence_rejected && result.checks == (31u & ~missing) &&
                            result.changed_ranges == changed && result.payload_bytes == 540,
                        "counter/payload/global/source/table mutation rejected");
    }
    OwnedProducer outer_counter;
    outer_counter.mutate_at = 20; // After both copies, at the outer counter read.
    outer_counter.mutation = 1;
    acquire(outer_counter, 0x10000, 0x30000, 1072, result);
    okay &= require(result.status == Status::coherence_rejected && result.checks == 15 && !result.changed_ranges,
                    "outer counter bracket is independently enforced");
    for (std::uint32_t index = 0; index < ranges.size(); ++index) {
        OwnedProducer torn;
        torn.mutate_at = 12;
        torn.mutation = 6 + index;
        acquire(torn, 0x10000, 0x30000, 1072, result);
        okay &= require(result.status == Status::coherence_rejected && result.changed_ranges == (1u << index),
                        "each selected range independently rejects a changed copy");
    }
    OwnedProducer expired;
    expired.expire_at = 3;
    acquire(expired, 0x10000, 0x30000, 1072, result);
    okay &= require(result.status == Status::deadline_expired && expired.calls == 3,
                    "cooperative deadline rejects before next read");
    for (std::uint32_t offset : {0u, 7u, 9u, 4104u}) {
        OwnedProducer invalid;
        acquire(invalid, 0x10000, 0x30000, offset, result);
        okay &= require(result.status == Status::invalid_arguments && invalid.calls == 0, "offset bound before reads");
    }
    OwnedProducer null_root;
    null_root.root = 0;
    acquire(null_root, 0x10000, 0x30000, 1072, result);
    okay &= require(result.status == Status::association_unavailable && null_root.calls == 1, "null root rejection");
    OwnedProducer wrong_table;
    wrong_table.table += 8;
    acquire(wrong_table, 0x10000, 0x30000, 1072, result);
    okay &= require(result.status == Status::association_unavailable && wrong_table.calls == 2,
                    "initial table mismatch rejection");
    OwnedProducer null_source;
    null_source.source = 0;
    acquire(null_source, 0x10000, 0x30000, 1072, result);
    okay &= require(result.status == Status::source_unavailable && null_source.calls == 3, "null source rejection");
    OwnedProducer overflow_source;
    overflow_source.source = UINT64_MAX;
    acquire(overflow_source, 0x10000, 0x30000, 1072, result);
    okay &= require(result.status == Status::invalid_arguments && overflow_source.calls == 3,
                    "source range overflow rejected before field reads");
    // API rejects invalid handles without process access and resets stale result.
    result.checks = 31;
    odv_read_selected_v1(nullptr, 0x10000, 0x30000, 1072, &result);
    okay &= require(result.version == 1 && result.status == Status::invalid_arguments && !result.checks &&
                        !result.read_calls,
                    "invalid handle resets result");
    if (okay)
        std::cout << "Owned mutation, rejection, read-bound and ABI tests passed\n";
    return okay ? 0 : 1;
}
