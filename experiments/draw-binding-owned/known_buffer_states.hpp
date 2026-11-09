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
        D3D12_BARRIER_ACCESS access_before{}, access_after{};
        bool enhanced = false;
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
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
                // A legacy UAV barrier does not change a known legacy state,
                // but cannot preserve proof of enhanced-only buffer usage.
                if (!barrier.UAV.pResource)
                    invalidate_all();
                else if (const auto *entry = find(barrier.UAV.pResource); entry && entry->enhanced)
                    forget(barrier.UAV.pResource);
                continue;
            }
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
            entry->enhanced = false;
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
    // Enhanced access scopes are NOT persistent legacy resource states. Retain
    // only proof of the barrier family and completion of a full-buffer barrier.
    // Copies must establish their own complete sync/access scope.
    void observe_enhanced(UINT32 count, const D3D12_BARRIER_GROUP *groups) {
        if (overflow_)
            return;
        UINT64 total = 0;
        if ((count && !groups) || count > 4096) {
            invalidate_all();
            return;
        }
        for (UINT32 i = 0; i < count; ++i) {
            const auto &group = groups[i];
            total += group.NumBarriers;
            if (total > 4096 || (group.NumBarriers && !group.pBufferBarriers)) {
                invalidate_all();
                return;
            }
        }
        for (UINT32 i = 0; i < count; ++i) {
            const auto &group = groups[i];
            if (group.Type == D3D12_BARRIER_TYPE_TEXTURE)
                continue; // No buffer layout/access transition.
            if (group.Type != D3D12_BARRIER_TYPE_BUFFER) {
                invalidate_all(); // Global/unknown scopes cannot prove a buffer mode.
                continue;
            }
            for (UINT32 j = 0; j < group.NumBarriers; ++j) {
                const auto &barrier = group.pBufferBarriers[j];
                auto *resource = barrier.pResource;
                if (!resource || resource->GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
                    continue;
                auto *entry = find(resource);
                if (!entry) {
                    if (size_ == entries_.size()) {
                        invalidate_all();
                        overflow_ = true;
                        return;
                    }
                    entry = &entries_[size_++];
                    *entry = {};
                    entry->resource = resource;
                }
                const bool matching_split = entry->enhanced && entry->phase == Phase::split &&
                                            entry->access_before == barrier.AccessBefore &&
                                            entry->access_after == barrier.AccessAfter;
                entry->enhanced = true;
                entry->phase = Phase::unknown;
                if (barrier.Offset != 0 || (barrier.Size != UINT64_MAX && barrier.Size != resource->GetDesc().Width))
                    continue;
                if (barrier.SyncAfter == D3D12_BARRIER_SYNC_SPLIT && barrier.SyncBefore != D3D12_BARRIER_SYNC_SPLIT) {
                    entry->access_before = barrier.AccessBefore;
                    entry->access_after = barrier.AccessAfter;
                    entry->phase = Phase::split;
                } else if (barrier.SyncAfter != D3D12_BARRIER_SYNC_SPLIT &&
                           barrier.SyncAfter != D3D12_BARRIER_SYNC_NONE &&
                           (barrier.SyncBefore != D3D12_BARRIER_SYNC_SPLIT || matching_split)) {
                    entry->access_after = barrier.AccessAfter;
                    entry->phase = Phase::known;
                }
            }
        }
    }
    bool enhanced_constant_scope(ID3D12Resource *resource) const noexcept {
        if (overflow_)
            return false;
        for (std::size_t i = 0; i < size_; ++i) {
            const auto &entry = entries_[i];
            if (entry.resource == resource && entry.enhanced && entry.phase == Phase::known)
                return entry.access_after == D3D12_BARRIER_ACCESS_COMMON ||
                       (entry.access_after & D3D12_BARRIER_ACCESS_CONSTANT_BUFFER) != 0;
        }
        return false;
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
            if (entry.resource == resource && !entry.enhanced && entry.phase == Phase::known &&
                (state & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) && !(state & ~read_only))
                return entry.state;
        }
        return {};
    }
};
} // namespace odv::research
