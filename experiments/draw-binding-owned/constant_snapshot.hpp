// SPDX-License-Identifier: MIT
#pragma once
#define NOMINMAX
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <optional>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>

namespace odv::research {
// Experimental GPU copy for an UPLOAD constant-buffer range. The caller must
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
    ConstantSnapshot() = default;
    ConstantSnapshot(const ConstantSnapshot &) = delete;
    ConstantSnapshot &operator=(const ConstantSnapshot &) = delete;
    ~ConstantSnapshot() {
        const bool unfinished = phase_ == Phase::recorded || phase_ == Phase::submitted;
        const auto completed = fence_ ? fence_->GetCompletedValue() : 0;
        if (unfinished && (!fence_ || completed == UINT64_MAX || completed < fence_value_)) {
            // Do not free GPU-referenced memory when its completion is unknown.
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
        if (phase_ != Phase::empty || !device || !commands || !source || !bytes || bytes > maximum_bytes)
            return false;
        if (commands->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE)
            return false;
        const auto desc = source->GetDesc();
        D3D12_HEAP_PROPERTIES heap{};
        D3D12_HEAP_FLAGS flags{};
        if (FAILED(source->GetHeapProperties(&heap, &flags)) || heap.Type != D3D12_HEAP_TYPE_UPLOAD ||
            desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || desc.Width > maximum_source_bytes ||
            offset > desc.Width || bytes > desc.Width - offset)
            return false;
        // UPLOAD resources have fixed GENERIC_READ state, including COPY_SOURCE.
        // DEFAULT/unknown states are rejected; no game resource is transitioned.
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
            commands->CopyBufferRegion(readback_.Get(), 0, source_.Get(), offset, bytes);
            phase_ = Phase::recorded;
            return true;
        } catch (...) {
            source_.Reset();
            readback_.Reset();
            release_count();
            throw;
        }
    }
    bool submitted_after_execute(ID3D12Fence *fence, UINT64 value) {
        if (phase_ != Phase::recorded || !fence || !value)
            return false;
        fence_ = fence;
        fence_value_ = value;
        phase_ = Phase::submitted;
        return true;
    }
    const std::optional<std::vector<std::byte>> &read_completed() {
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
        source_.Reset();
        readback_.Reset();
        fence_.Reset();
        release_count();
        return data_;
    }
    // Only after the caller proves this recording will never be submitted.
    bool discard_unsubmitted() noexcept {
        if (phase_ != Phase::recorded)
            return false;
        phase_ = Phase::discarded;
        source_.Reset();
        readback_.Reset();
        release_count();
        return true;
    }
};
} // namespace odv::research
