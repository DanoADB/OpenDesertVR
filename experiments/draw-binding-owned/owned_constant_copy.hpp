// SPDX-License-Identifier: MIT
#pragma once
#include "constant_snapshot.hpp"

namespace odv::research {
// A single owned copy submission, for a valid UPLOAD source observed during
// an application callback. Submit on the actual app queue BEFORE its upcoming
// ExecuteCommandLists; this preserves that queue's preceding waits. Never use
// an unrelated queue or claim these bytes are a later DEFAULT-buffer state.
class OwnedConstantCopy final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    ConstantSnapshot snapshot_;
    Com<ID3D12CommandAllocator> allocator_;
    Com<ID3D12GraphicsCommandList> commands_;
    bool executed_ = false, retired_ = false;
    static void checked(HRESULT result) {
        if (FAILED(result))
            throw std::runtime_error("owned_constant_copy_graphics_failure");
    }

  public:
    OwnedConstantCopy() = default;
    OwnedConstantCopy(const OwnedConstantCopy &) = delete;
    OwnedConstantCopy &operator=(const OwnedConstantCopy &) = delete;
    ~OwnedConstantCopy() {
        if (executed_ && !retired_) {
            // Keep command storage alive along with ConstantSnapshot's bounded
            // quarantine if completion cannot be established. No CPU wait.
            allocator_.Detach();
            commands_.Detach();
        } else if (!executed_) {
            (void)snapshot_.discard_unsubmitted();
        }
    }
    bool submit(ID3D12CommandQueue *queue, ID3D12Resource *source, UINT64 offset, std::size_t bytes) {
        if (!queue || commands_ || executed_ || retired_)
            return false;
        const auto type = queue->GetDesc().Type;
        if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE &&
            type != D3D12_COMMAND_LIST_TYPE_COPY)
            return false;
        Com<ID3D12Device> device;
        checked(queue->GetDevice(IID_PPV_ARGS(&device)));
        checked(device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator_)));
        checked(device->CreateCommandList(0, type, allocator_.Get(), nullptr, IID_PPV_ARGS(&commands_)));
        if (!snapshot_.record(device.Get(), commands_.Get(), source, offset, bytes)) {
            commands_.Reset();
            allocator_.Reset();
            return false;
        }
        checked(commands_->Close());
        Com<ID3D12Fence> fence;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        (void)snapshot_.before_execute();
        ID3D12CommandList *execute[] = {commands_.Get()};
        executed_ = true;
        queue->ExecuteCommandLists(1, execute);
        // This is AFTER our owned copy's native execution submission, even
        // though the upcoming application command list has not executed yet.
        checked(queue->Signal(fence.Get(), 1));
        if (!snapshot_.submitted_after_execute(fence.Get(), 1))
            throw std::runtime_error("owned_constant_copy_submission_not_tracked");
        return true;
    }
    const std::optional<std::vector<std::byte>> &read_completed() {
        const auto &data = snapshot_.read_completed();
        if (data && !retired_) {
            // We exclusively own this recording and never execute it again.
            // Its completed fence also permits allocator/list destruction.
            commands_.Reset();
            allocator_.Reset();
            retired_ = snapshot_.release_retired();
        }
        return data;
    }
};
} // namespace odv::research
