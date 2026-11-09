// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <cstdint>
#include <d3d12.h>

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
    std::uint32_t capabilities_ = 0;
    std::array<D3D12_STREAM_OUTPUT_BUFFER_VIEW, 4> stream_outputs_{};

  public:
    void reset() noexcept {
        queries_ = {};
        query_count_ = 0;
        failed_ = active_pass_ = suspended_ = suspending_ = false;
        stream_outputs_ = {};
    }
    void invalidate() noexcept { failed_ = true; }
    void capabilities(std::uint32_t value) noexcept { capabilities_ = value; }
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
    bool eligible() const noexcept {
        return !failed_ && capabilities_ == 7 && !active_pass_ && !suspended_ && !query_count_;
    }
    bool failed() const noexcept { return failed_; }
    UINT active_queries() const noexcept { return query_count_; }
    bool render_pass_blocked() const noexcept { return active_pass_ || suspended_; }
    const auto &stream_outputs() const noexcept { return stream_outputs_; }
};
} // namespace odv::research
