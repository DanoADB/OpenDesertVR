// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "deferred_resource_release.hpp"
#include "known_buffer_states.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include <wrl/client.h>

namespace odv::research {
// Experimental GPU copy for a bounded constant-buffer range. The caller must
// establish actual submission and a fence ordered AFTER this copy; a recorded
// command or a before-execute callback alone does not establish that ordering.
class ConstantSnapshot final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    enum class Phase { empty, recorded, submitted, complete, discarded };
    Phase phase_ = Phase::empty;
    Com<ID3D12Resource> source_, readback_;
    Com<ID3D12Fence> fence_;
    UINT64 fence_value_ = 0;
    std::size_t bytes_ = 0;
    std::optional<std::vector<std::byte>> data_;
    bool counted_ = false;
    bool execution_observed_ = false;
    static inline std::atomic_uint32_t pending_ = 0;
    void release_count() noexcept {
        if (counted_) {
            --pending_;
            counted_ = false;
        }
    }
    static void checked(HRESULT result) {
        if (FAILED(result))
            throw std::runtime_error("constant_snapshot_graphics_failure");
    }

  public:
    static constexpr std::size_t maximum_bytes = 4096;
    static constexpr UINT64 maximum_source_bytes = 64ULL * 1024 * 1024;
    static constexpr uint32_t maximum_pending = 6;
    struct SourceInfo {
        const char *state = "INVALID_SOURCE";
        uint32_t heap_type = 0;
        UINT64 width = 0;
        bool supported = false;
    };
    // Metadata only, during a callback where the native source is valid.
    static SourceInfo inspect_source(ID3D12Resource *source, UINT64 offset, std::size_t bytes) {
        SourceInfo info;
        if (!source)
            return info;
        const auto desc = source->GetDesc();
        info.width = desc.Width;
        D3D12_HEAP_PROPERTIES heap{};
        D3D12_HEAP_FLAGS flags{};
        if (FAILED(source->GetHeapProperties(&heap, &flags))) {
            info.state = "HEAP_QUERY_FAILED";
            return info;
        }
        info.heap_type = static_cast<uint32_t>(heap.Type);
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            info.state = "NOT_BUFFER";
        else if (heap.Type != D3D12_HEAP_TYPE_UPLOAD)
            info.state = "HEAP_NOT_UPLOAD";
        else if (desc.Width > maximum_source_bytes)
            info.state = "SOURCE_ALLOCATION_TOO_LARGE";
        else if (!bytes || bytes > maximum_bytes || offset > desc.Width || bytes > desc.Width - offset)
            info.state = "RANGE_REJECTED";
        else {
            info.state = "ELIGIBLE_UPLOAD";
            info.supported = true;
        }
        return info;
    }
    static SourceInfo inspect_default_source(ID3D12Resource *source, UINT64 offset, std::size_t bytes,
                                             const KnownBufferStates &states) {
        auto info = inspect_source(source, offset, bytes);
        info.supported = false;
        if (info.heap_type != D3D12_HEAP_TYPE_DEFAULT) {
            info.state = "HEAP_NOT_DEFAULT";
        } else if (std::strcmp(info.state, "HEAP_NOT_UPLOAD") != 0) {
            return info; // Buffer/heap query already failed.
        } else if (info.width > maximum_source_bytes) {
            info.state = "SOURCE_ALLOCATION_TOO_LARGE";
        } else if (!bytes || bytes > maximum_bytes || offset > info.width || bytes > info.width - offset) {
            info.state = "RANGE_REJECTED";
        } else if (states.enhanced_constant_scope(source)) {
            info.state = "ELIGIBLE_DEFAULT_ENHANCED_SCOPE";
            info.supported = true;
        } else if (!states.constant_read_state(source)) {
            info.state = "DEFAULT_STATE_UNVERIFIED";
        } else {
            info.state = "ELIGIBLE_DEFAULT_KNOWN_READ_STATE";
            info.supported = true;
        }
        return info;
    }
    ConstantSnapshot() = default;
    ConstantSnapshot(const ConstantSnapshot &) = delete;
    ConstantSnapshot &operator=(const ConstantSnapshot &) = delete;
    ~ConstantSnapshot() {
        if (counted_) {
            // A completed command list may execute again. Only explicit
            // recording retirement plus completion permits resource release.
            // Retain the capacity slot too: at most six abandoned captures can
            // exist, after which capture stops. This is not a hard watchdog.
            source_.Detach();
            readback_.Detach();
            fence_.Detach();
            return;
        }
        release_count();
    }
    bool record(ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *source, UINT64 offset,
                std::size_t bytes) {
        return record_copy(device, commands, source, offset, bytes, std::nullopt);
    }
    // Experimental DEFAULT path. Requires exact native state observations in
    // THIS command recording; never infer states from descriptors or CBV use.
    // Enhanced barriers use their own sync/access scopes, never legacy states.
    bool record_default(ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *source,
                        UINT64 offset, std::size_t bytes, const KnownBufferStates &states) {
        const auto state = states.constant_read_state(source);
        const bool enhanced = states.enhanced_constant_scope(source);
        if ((!state && !enhanced) || !commands || commands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return false;
        return record_copy(device, commands, source, offset, bytes, state, enhanced);
    }

  private:
    bool record_copy(ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *source, UINT64 offset,
                     std::size_t bytes, std::optional<D3D12_RESOURCE_STATES> default_state, bool enhanced = false) {
        if (phase_ != Phase::empty || !device || !commands || !source || !bytes || bytes > maximum_bytes)
            return false;
        if (commands->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE)
            return false;
        Com<ID3D12GraphicsCommandList7> enhanced_commands;
        if (enhanced) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options))) ||
                !options.EnhancedBarriersSupported ||
                FAILED(commands->QueryInterface(IID_PPV_ARGS(&enhanced_commands))))
                return false;
        }
        if (default_state || enhanced) {
            const auto desc = source->GetDesc();
            D3D12_HEAP_PROPERTIES heap{};
            D3D12_HEAP_FLAGS flags{};
            if (FAILED(source->GetHeapProperties(&heap, &flags)) || heap.Type != D3D12_HEAP_TYPE_DEFAULT ||
                (flags & D3D12_HEAP_FLAG_HARDWARE_PROTECTED) || desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
                desc.Width > maximum_source_bytes || offset > desc.Width || bytes > desc.Width - offset)
                return false;
        } else if (!inspect_source(source, offset, bytes).supported) {
            return false;
        }
        // UPLOAD resources have fixed GENERIC_READ state, including COPY_SOURCE.
        // The separate DEFAULT path temporarily adds COPY_SOURCE to an exact
        // known read state and restores that state in the same recording.
        if (pending_.fetch_add(1) >= maximum_pending) {
            --pending_;
            return false;
        }
        counted_ = true;
        try {
            D3D12_RESOURCE_DESC target{};
            target.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            target.Width = bytes;
            target.Height = 1;
            target.DepthOrArraySize = 1;
            target.MipLevels = 1;
            target.SampleDesc.Count = 1;
            target.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES read_heap{};
            read_heap.Type = D3D12_HEAP_TYPE_READBACK;
            checked(device->CreateCommittedResource(&read_heap, D3D12_HEAP_FLAG_NONE, &target,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)));
            source_ = source;
            bytes_ = bytes;
            D3D12_RESOURCE_BARRIER barrier{};
            D3D12_BUFFER_BARRIER enhanced_barrier{};
            D3D12_BARRIER_GROUP group{};
            if (enhanced) {
                // COMMON covers all preceding writes. This deliberately broad
                // flush avoids pretending the app's last AccessAfter describes
                // every intervening access. Buffers have no layout to restore.
                enhanced_barrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
                enhanced_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
                enhanced_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
                enhanced_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
                enhanced_barrier.pResource = source;
                enhanced_barrier.Size = UINT64_MAX;
                group.Type = D3D12_BARRIER_TYPE_BUFFER;
                group.NumBarriers = 1;
                group.pBufferBarriers = &enhanced_barrier;
                enhanced_commands->Barrier(1, &group);
            }
            const bool transition = default_state && !(*default_state & D3D12_RESOURCE_STATE_COPY_SOURCE);
            if (transition) {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = source;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = *default_state;
                barrier.Transition.StateAfter = *default_state | D3D12_RESOURCE_STATE_COPY_SOURCE;
                commands->ResourceBarrier(1, &barrier);
            }
            commands->CopyBufferRegion(readback_.Get(), 0, source_.Get(), offset, bytes);
            if (enhanced) {
                enhanced_barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
                enhanced_barrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
                enhanced_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
                enhanced_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
                enhanced_commands->Barrier(1, &group);
            }
            if (transition) {
                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                commands->ResourceBarrier(1, &barrier);
            }
            phase_ = Phase::recorded;
            return true;
        } catch (...) {
            source_.Reset();
            readback_.Reset();
            release_count();
            throw;
        }
    }

  public:
    // Invoke before EVERY native execution, while excluding concurrent reads.
    // No new read is permitted until an after-execute fence covers this call.
    // A previously copied CPU sample stays immutable across resubmission.
    bool before_execute() noexcept {
        if (phase_ != Phase::recorded && phase_ != Phase::submitted && phase_ != Phase::complete)
            return false;
        execution_observed_ = true;
        phase_ = Phase::recorded;
        return true;
    }
    bool submitted_after_execute(ID3D12Fence *fence, UINT64 value) {
        if ((phase_ != Phase::recorded && phase_ != Phase::submitted && phase_ != Phase::complete) || !fence || !value)
            return false;
        execution_observed_ = true;
        fence_ = fence;
        fence_value_ = value;
        phase_ = Phase::submitted;
        return true;
    }
    const std::optional<std::vector<std::byte>> &read_completed() {
        if (data_)
            return data_;
        if (phase_ != Phase::submitted)
            return data_;
        const auto completed = fence_->GetCompletedValue();
        if (completed == UINT64_MAX)
            throw std::runtime_error("constant_snapshot_device_removed");
        if (completed < fence_value_)
            return data_;
        void *mapped = nullptr;
        const D3D12_RANGE range{0, bytes_};
        checked(readback_->Map(0, &range, &mapped));
        try {
            std::vector<std::byte> values(bytes_);
            std::memcpy(values.data(), mapped, bytes_);
            data_ = std::move(values);
        } catch (...) {
            const D3D12_RANGE empty{};
            readback_->Unmap(0, &empty);
            throw;
        }
        const D3D12_RANGE empty{};
        readback_->Unmap(0, &empty);
        phase_ = Phase::complete;
        return data_;
    }
    // Caller proves the command recording can NEVER execute again (successful
    // reset or destruction), and has reported every execution. The last fence
    // must cover all of them. A before-reset callback alone is insufficient.
    bool release_retired() noexcept {
        if ((phase_ != Phase::submitted && phase_ != Phase::complete) || !fence_)
            return false;
        const auto completed = fence_->GetCompletedValue();
        if (completed == UINT64_MAX || completed < fence_value_)
            return false;
        if (!DeferredResourceRelease::defer(source_, readback_))
            return false;
        fence_.Reset();
        phase_ = Phase::discarded;
        release_count();
        return true;
    }
    // Only after the caller proves this recording will never be submitted.
    bool discard_unsubmitted() noexcept {
        if (phase_ != Phase::recorded || execution_observed_)
            return false;
        if (!DeferredResourceRelease::defer(source_, readback_))
            return false;
        phase_ = Phase::discarded;
        release_count();
        return true;
    }
};
} // namespace odv::research
