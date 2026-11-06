// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <d3d12.h>
#include <optional>

namespace odv::research {
// One command recording, not global execution state. Feed exact native legacy
// barriers AFTER recording them. An abstract before/after-usage callback that
// drops split flags is insufficient. Reset/invalidate on recording replacement,
// unobserved bundle/barrier APIs; forget resources on destruction/recreation.
// Caller serializes access as required for D3D12 command-list recording.
class KnownBufferStates final {
    enum class Phase { unknown, known, split };
    struct Entry {
        ID3D12Resource *resource = nullptr; // Identity only, no retained COM ref.
        D3D12_RESOURCE_STATES state{}, before{}, after{};
        Phase phase = Phase::unknown;
    };
    std::array<Entry, 128> entries_{};
    std::size_t size_ = 0;
    bool overflow_ = false;
    Entry *find(ID3D12Resource *resource) {
        for (std::size_t i = 0; i < size_; ++i)
            if (entries_[i].resource == resource)
                return &entries_[i];
        return nullptr;
    }

  public:
    void reset() noexcept {
        size_ = 0;
        overflow_ = false;
    }
    void invalidate_all() noexcept { size_ = 0; }
    void forget(ID3D12Resource *resource) noexcept {
        for (std::size_t i = 0; i < size_; ++i)
            if (entries_[i].resource == resource) {
                entries_[i] = entries_[--size_];
                return;
            }
    }
    void observe(UINT count, const D3D12_RESOURCE_BARRIER *barriers) {
        if (overflow_)
            return;
        if (count && !barriers) {
            invalidate_all();
            return;
        }
        for (UINT i = 0; i < count; ++i) {
            const auto &barrier = barriers[i];
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING) {
                if (!barrier.Aliasing.pResourceBefore || !barrier.Aliasing.pResourceAfter)
                    invalidate_all();
                else {
                    forget(barrier.Aliasing.pResourceBefore);
                    forget(barrier.Aliasing.pResourceAfter);
                }
                continue;
            }
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
                continue; // Ordering only; does not establish/change a state.
            if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
                invalidate_all();
                continue;
            }
            const auto &transition = barrier.Transition;
            auto *resource = transition.pResource;
            if (!resource || resource->GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
                continue;
            auto *entry = find(resource);
            if (!entry) {
                if (size_ == entries_.size()) {
                    invalidate_all();
                    overflow_ = true; // No partial state proof after overflow.
                    return;
                }
                entry = &entries_[size_++];
                *entry = {};
                entry->resource = resource;
            }
            // Buffers have only subresource zero. Unknown coverage is rejected.
            if (transition.Subresource != 0 && transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
                entry->phase = Phase::unknown;
                continue;
            }
            if (barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_NONE) {
                entry->state = transition.StateAfter;
                entry->phase = Phase::known;
            } else if (barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY) {
                entry->before = transition.StateBefore;
                entry->after = transition.StateAfter;
                entry->phase = Phase::split;
            } else if (barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_END_ONLY && entry->phase == Phase::split &&
                       entry->before == transition.StateBefore && entry->after == transition.StateAfter) {
                entry->state = transition.StateAfter;
                entry->phase = Phase::known;
            } else {
                entry->phase = Phase::unknown;
            }
        }
    }
    std::optional<D3D12_RESOURCE_STATES> constant_read_state(ID3D12Resource *resource) const noexcept {
        constexpr unsigned read_only = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
                                       D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |
                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (overflow_)
            return {};
        for (std::size_t i = 0; i < size_; ++i) {
            const auto &entry = entries_[i];
            const auto state = static_cast<unsigned>(entry.state);
            if (entry.resource == resource && entry.phase == Phase::known &&
                (state & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) && !(state & ~read_only))
                return entry.state;
        }
        return {};
    }
};
} // namespace odv::research
