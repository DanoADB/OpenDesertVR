// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <optional>

namespace odv::research {
// Bounded metadata only. Native creation/reset establishes empty state; dropped,
// unsupported or mismatched events invalidate proof until a new recording.
// Caller serializes notifications and reads. SO GPU addresses still need to be
// resolved/validated while preparing replay; this class retains no COM objects.
class DrawRecordingState final {
    struct Query {
        ID3D12QueryHeap *heap = nullptr;
        D3D12_QUERY_TYPE type{};
        UINT index = 0;
    };
    std::array<Query, 64> queries_{};
    UINT query_count_ = 0;
    bool failed_ = true, active_pass_ = false, suspended_ = false, suspending_ = false;
    bool predicated_ = false;
    std::uint32_t capabilities_ = 0;
    std::array<D3D12_STREAM_OUTPUT_BUFFER_VIEW, 4> stream_outputs_{};

  public:
    void reset() noexcept {
        queries_ = {};
        query_count_ = 0;
        failed_ = active_pass_ = suspended_ = suspending_ = false;
        stream_outputs_ = {};
        predicated_ = false; // Direct recordings start with predication disabled.
    }
    void invalidate() noexcept { failed_ = true; }
    void capabilities(std::uint32_t value) noexcept { capabilities_ = value; }
    void predication(bool bound, UINT64 offset, D3D12_PREDICATION_OP operation) noexcept {
        if ((bound && offset % 8 != 0) ||
            (operation != D3D12_PREDICATION_OP_EQUAL_ZERO && operation != D3D12_PREDICATION_OP_NOT_EQUAL_ZERO)) {
            invalidate();
            return;
        }
        predicated_ = bound;
    }
    bool predication_known() const noexcept { return !failed_ && capabilities_ == 15; }
    bool predicate_bound() const noexcept { return predicated_; }
    // CopyBufferRegion is predicated. Completion alone cannot prove it ran.
    // This guard never disables/rebinds the application's predicate and never
    // assumes its GPU value. Active queries are independent of this copy gate.
    bool copy_unpredicated() const noexcept {
        return predication_known() && !predicated_ && !active_pass_ && !suspended_;
    }
    void stream_output(UINT first, UINT count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views) noexcept {
        if (!count || first > stream_outputs_.size() || count > stream_outputs_.size() - first) {
            invalidate();
            return;
        }
        for (UINT i = 0; i < count; ++i)
            stream_outputs_[first + i] = views ? views[i] : D3D12_STREAM_OUTPUT_BUFFER_VIEW{};
    }
    void query(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index, bool begin) noexcept {
        if (!heap || (begin && type == D3D12_QUERY_TYPE_TIMESTAMP)) {
            invalidate();
            return;
        }
        if (type == D3D12_QUERY_TYPE_TIMESTAMP)
            return; // Timestamp is EndQuery-only, never an active interval.
        Query *matching = nullptr, *empty = nullptr;
        for (auto &entry : queries_) {
            if (entry.heap == heap && entry.type == type && entry.index == index)
                matching = &entry;
            if (!entry.heap && !empty)
                empty = &entry;
        }
        if ((begin && (matching || !empty)) || (!begin && !matching)) {
            invalidate();
            return;
        }
        if (begin) {
            *empty = {heap, type, index};
            ++query_count_;
        } else {
            *matching = {};
            --query_count_;
        }
    }
    void render_pass(bool begin, D3D12_RENDER_PASS_FLAGS flags = D3D12_RENDER_PASS_FLAG_NONE) noexcept {
        if (begin == active_pass_) {
            invalidate();
            return;
        }
        active_pass_ = begin;
        if (begin) {
            // A resumed pass may originate in another recording. Capture is
            // prohibited inside it and between suspend/resume boundaries.
            if (suspended_ && !(flags & D3D12_RENDER_PASS_FLAG_RESUMING_PASS))
                invalidate();
            suspended_ = false;
            suspending_ = (flags & D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS) != 0;
        } else {
            suspended_ = suspending_;
            suspending_ = false;
        }
    }
    bool eligible() const noexcept { return eligible_single_forward() && !query_count_ && copy_unpredicated(); }
    // A replacement forwards the application's existing command exactly once.
    // Active queries still observe that command; replay/copy users retain the
    // stricter eligible() gate. Unknown query lifetimes remain fail-closed.
    bool eligible_single_forward() const noexcept {
        return !failed_ && (capabilities_ == 7 || capabilities_ == 15) && !active_pass_ && !suspended_;
    }
    bool failed() const noexcept { return failed_; }
    // Select a query family not already active in the application's recording.
    // Keep binary/full occlusion and both statistics versions in one family.
    std::optional<D3D12_QUERY_TYPE> work_query_type() const noexcept {
        if (!eligible_single_forward())
            return {};
        bool statistics = false, occlusion = false;
        for (const auto &query : queries_) {
            if (!query.heap)
                continue;
            switch (query.type) {
            case D3D12_QUERY_TYPE_PIPELINE_STATISTICS:
            case D3D12_QUERY_TYPE_PIPELINE_STATISTICS1:
                statistics = true;
                break;
            case D3D12_QUERY_TYPE_OCCLUSION:
            case D3D12_QUERY_TYPE_BINARY_OCCLUSION:
                occlusion = true;
                break;
            case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0:
            case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1:
            case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2:
            case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3:
                break;
            default:
                return {};
            }
        }
        if (!statistics)
            return D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
        if (!occlusion)
            return D3D12_QUERY_TYPE_OCCLUSION;
        return {};
    }
    UINT active_queries() const noexcept { return query_count_; }
    bool render_pass_blocked() const noexcept { return active_pass_ || suspended_; }
    const auto &stream_outputs() const noexcept { return stream_outputs_; }
};
} // namespace odv::research
