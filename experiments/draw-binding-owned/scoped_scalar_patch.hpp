// SPDX-License-Identifier: MIT
#pragma once
#include "constant_snapshot.hpp"
#include <cmath>

namespace odv::research {
// Experimental four-byte GPU perturbation around ONE actual native draw.
// The original bits are saved/restored on the GPU in the same recording. This
// proves an input consumer, not a complete camera or independent eye render.
// Caller must preserve the original draw, report EVERY execution, and retire
// the recording before releasing resources. Never replay guessed indirect args.
// OWNED EXCLUSIVE SOURCES ONLY: barriers in one queue do not order another
// queue's reads. The game camera buffer's cross-queue exclusivity is unproven;
// the in-place entry point must not be enabled on that buffer. Game integration
// needs a private copied constant buffer and scoped binding replacement.
class ScopedScalarPatch final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    Com<ID3D12Resource> source_, backup_, upload_, original_;
    Com<ID3D12Fence> fence_;
    UINT64 offset_ = 0, fence_value_ = 0;
    ID3D12GraphicsCommandList7 *commands_ = nullptr; // Recording identity, no ownership.
    bool recorded_ = false, restored_ = false, executed_ = false, covered_ = false, counted_ = false;
    static inline std::atomic_uint32_t pending_ = 0;
    static void checked(HRESULT result) {
        if (FAILED(result))
            throw std::runtime_error("scalar_patch_graphics_failure");
    }
    static void barrier(ID3D12GraphicsCommandList7 *commands, ID3D12Resource *resource, D3D12_BARRIER_SYNC before,
                        D3D12_BARRIER_SYNC after, D3D12_BARRIER_ACCESS access_before,
                        D3D12_BARRIER_ACCESS access_after) noexcept {
        D3D12_BUFFER_BARRIER buffer{};
        buffer.SyncBefore = before;
        buffer.SyncAfter = after;
        buffer.AccessBefore = access_before;
        buffer.AccessAfter = access_after;
        buffer.pResource = resource;
        buffer.Size = UINT64_MAX;
        D3D12_BARRIER_GROUP group{};
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = 1;
        group.pBufferBarriers = &buffer;
        commands->Barrier(1, &group);
    }

  public:
    static constexpr uint32_t maximum_pending = 6;
    ScopedScalarPatch() = default;
    ScopedScalarPatch(const ScopedScalarPatch &) = delete;
    ScopedScalarPatch &operator=(const ScopedScalarPatch &) = delete;
    ~ScopedScalarPatch() {
        if (recorded_) {
            // Unknown retirement/completion: bounded quarantine, including slot.
            source_.Detach();
            backup_.Detach();
            upload_.Detach();
            original_.Detach();
            fence_.Detach();
        } else if (counted_) {
            --pending_;
        }
    }
    bool begin(ID3D12Device *device, ID3D12GraphicsCommandList7 *commands, ID3D12Resource *source, UINT64 offset,
               float replacement, const KnownBufferStates &states) {
        return begin_impl(device, commands, source, offset, 0, 0, replacement, states);
    }
    // Read-only original input. Caller binds this private GPU address around
    // the actual draw and restores ALL affected binding state afterwards.
    bool begin_private(ID3D12Device *device, ID3D12GraphicsCommandList7 *commands, ID3D12Resource *source,
                       UINT64 source_offset, std::size_t bytes, UINT64 field_offset, float replacement,
                       const KnownBufferStates &states) {
        if (!bytes || bytes > ConstantSnapshot::maximum_bytes || bytes % 256 || field_offset % 4 ||
            field_offset > bytes || 4 > bytes - field_offset)
            return false;
        return begin_impl(device, commands, source, source_offset, bytes, field_offset, replacement, states);
    }
    D3D12_GPU_VIRTUAL_ADDRESS private_gpu_address() const noexcept {
        return original_ && source_ ? source_->GetGPUVirtualAddress() : 0;
    }
    ID3D12Resource *private_buffer() const noexcept { return original_ ? source_.Get() : nullptr; }

  private:
    bool begin_impl(ID3D12Device *device, ID3D12GraphicsCommandList7 *commands, ID3D12Resource *source, UINT64 offset,
                    std::size_t clone_bytes, UINT64 field_offset, float replacement, const KnownBufferStates &states) {
        if (recorded_ || counted_ || !device || !commands || !std::isfinite(replacement) || offset % 4 ||
            commands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT || !states.enhanced_constant_scope(source) ||
            !ConstantSnapshot::inspect_default_source(source, offset, clone_bytes ? clone_bytes : 4, states).supported)
            return false;
        D3D12_HEAP_PROPERTIES source_heap{};
        D3D12_HEAP_FLAGS flags{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
        if (FAILED(source->GetHeapProperties(&source_heap, &flags)) || (flags & D3D12_HEAP_FLAG_HARDWARE_PROTECTED) ||
            FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options))) ||
            !options.EnhancedBarriersSupported)
            return false;
        if (pending_.fetch_add(1) >= maximum_pending) {
            --pending_;
            return false;
        }
        counted_ = true;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 256;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (clone_bytes) {
            desc.Width = clone_bytes;
            checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                    nullptr, IID_PPV_ARGS(&source_)));
            desc.Width = 256;
        }
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                nullptr, IID_PPV_ARGS(&backup_)));
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr, IID_PPV_ARGS(&upload_)));
        void *mapped = nullptr;
        const D3D12_RANGE empty{};
        checked(upload_->Map(0, &empty, &mapped));
        std::memcpy(mapped, &replacement, sizeof(replacement));
        const D3D12_RANGE written{0, sizeof(replacement)};
        upload_->Unmap(0, &written);
        if (clone_bytes) {
            original_ = source;
            offset_ = field_offset;
        } else {
            source_ = source;
            offset_ = offset;
        }
        commands_ = commands;
        restored_ = executed_ = covered_ = false;
        // All allocations are complete. The remaining recording calls do not throw.
        if (clone_bytes) {
            barrier(commands, source, D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COMMON,
                    D3D12_BARRIER_ACCESS_COPY_SOURCE);
            barrier(commands, source_.Get(), D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_COPY,
                    D3D12_BARRIER_ACCESS_COMMON, D3D12_BARRIER_ACCESS_COPY_DEST);
            commands->CopyBufferRegion(source_.Get(), 0, source, offset, clone_bytes);
            barrier(commands, source, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_ACCESS_COPY_SOURCE,
                    D3D12_BARRIER_ACCESS_COMMON);
            barrier(commands, source_.Get(), D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_ALL,
                    D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_ACCESS_COMMON);
            source = source_.Get();
            offset = offset_;
        }
        barrier(commands, source, D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COMMON,
                D3D12_BARRIER_ACCESS_COPY_SOURCE);
        barrier(commands, backup_.Get(), D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COMMON,
                D3D12_BARRIER_ACCESS_COPY_DEST);
        commands->CopyBufferRegion(backup_.Get(), 0, source, offset, 4);
        barrier(commands, backup_.Get(), D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_ACCESS_COPY_SOURCE);
        barrier(commands, source, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE,
                D3D12_BARRIER_ACCESS_COPY_DEST);
        commands->CopyBufferRegion(source, offset, upload_.Get(), 0, 4);
        barrier(commands, source, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_ACCESS_COPY_DEST,
                D3D12_BARRIER_ACCESS_COMMON);
        recorded_ = true;
        return true;
    }

  public:
    bool end(ID3D12GraphicsCommandList7 *commands) noexcept {
        if (!recorded_ || restored_ || !commands || commands != commands_)
            return false;
        barrier(commands, source_.Get(), D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COMMON,
                D3D12_BARRIER_ACCESS_COPY_DEST);
        commands->CopyBufferRegion(source_.Get(), offset_, backup_.Get(), 0, 4);
        barrier(commands, source_.Get(), D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_ALL,
                D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_ACCESS_COMMON);
        barrier(commands, backup_.Get(), D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_ALL,
                D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_ACCESS_COMMON);
        restored_ = true;
        return true;
    }
    bool before_execute() noexcept {
        if (!recorded_ || !restored_)
            return false;
        executed_ = true;
        covered_ = false;
        return true;
    }
    bool submitted_after_execute(ID3D12Fence *fence, UINT64 value) {
        if (!executed_ || !restored_ || !fence || !value)
            return false;
        fence_ = fence;
        fence_value_ = value;
        covered_ = true;
        return true;
    }
    bool release_retired() noexcept {
        if (!covered_ || !fence_)
            return false;
        const auto completed = fence_->GetCompletedValue();
        if (completed == UINT64_MAX || completed < fence_value_)
            return false;
        if (!DeferredResourceRelease::defer(source_, backup_) || !DeferredResourceRelease::defer(upload_, original_))
            return false;
        fence_.Reset();
        recorded_ = false;
        counted_ = false;
        --pending_;
        return true;
    }
    // Only once the recording is retired and proven never submitted.
    bool discard_unsubmitted() noexcept {
        if (!recorded_ || executed_ || !restored_)
            return false;
        if (!DeferredResourceRelease::defer(source_, backup_) || !DeferredResourceRelease::defer(upload_, original_))
            return false;
        recorded_ = false;
        counted_ = false;
        --pending_;
        return true;
    }
};
} // namespace odv::research
