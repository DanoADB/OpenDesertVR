// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "constant_snapshot.hpp"
#include <array>
#include <dxgi1_4.h>
#include <iostream>
#include <memory>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static void check(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("owned_graphics_failure");
}
static void require(bool value) {
    if (!value)
        throw std::runtime_error("constant_snapshot_check_failed");
}
static void run_case(D3D12_COMMAND_LIST_TYPE queue_type) {
    Com<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Com<IDXGIAdapter> adapter;
    check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    Com<ID3D12Device> device;
    check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    Com<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = queue_type;
    check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 1024;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    Com<ID3D12Resource> source;
    check(device->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&source)));
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    check(source->Map(0, &empty, &mapped));
    std::array<float, 16> before{}, after{};
    for (unsigned i = 0; i < 16; ++i) {
        before[i] = static_cast<float>(i);
        after[i] = static_cast<float>(100 + i);
    }
    std::memcpy(static_cast<std::byte *>(mapped) + 256, before.data(), sizeof(before));
    std::array<Com<ID3D12CommandAllocator>, 2> allocators;
    std::array<Com<ID3D12GraphicsCommandList>, 2> commands;
    for (unsigned i = 0; i < 2; ++i) {
        check(device->CreateCommandAllocator(queue_type, IID_PPV_ARGS(&allocators[i])));
        check(device->CreateCommandList(0, queue_type, allocators[i].Get(), nullptr, IID_PPV_ARGS(&commands[i])));
    }
    using odv::research::ConstantSnapshot;
    ConstantSnapshot invalid, submitted, unsubmitted;
    D3D12_HEAP_PROPERTIES gpu_heap{};
    gpu_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Com<ID3D12Resource> gpu_only;
    check(device->CreateCommittedResource(&gpu_heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                          IID_PPV_ARGS(&gpu_only)));
    require(!invalid.record(device.Get(), commands[0].Get(), gpu_only.Get(), 0, sizeof(before)));
    require(!invalid.record(device.Get(), commands[0].Get(), source.Get(), 1000, sizeof(before)));
    require(!invalid.record(device.Get(), commands[0].Get(), source.Get(), 0, 4097));
    require(submitted.record(device.Get(), commands[0].Get(), source.Get(), 256, sizeof(before)));
    require(unsubmitted.record(device.Get(), commands[1].Get(), source.Get(), 256, sizeof(before)));
    std::array<std::unique_ptr<ConstantSnapshot>, 4> pending;
    for (auto &snapshot : pending) {
        snapshot = std::make_unique<ConstantSnapshot>();
        require(snapshot->record(device.Get(), commands[1].Get(), source.Get(), 256, sizeof(before)));
    }
    ConstantSnapshot over_capacity;
    require(!over_capacity.record(device.Get(), commands[1].Get(), source.Get(), 256, sizeof(before)));
    require(!submitted.read_completed() && !unsubmitted.read_completed());
    check(commands[0]->Close());
    check(commands[1]->Close());
    // A legal late write: after recording, before submission. A CPU sample
    // taken at record time would be stale. GPU copy must observe 'after'.
    std::memcpy(static_cast<std::byte *>(mapped) + 256, after.data(), sizeof(after));
    source->Unmap(0, nullptr);
    Com<ID3D12Fence> fence;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    Com<ID3D12Fence> gate;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    struct ReleaseGate {
        ID3D12Fence *fence;
        ~ReleaseGate() { fence->Signal(1); }
    } release_gate{gate.Get()};
    check(queue->Wait(gate.Get(), 1));
    ID3D12CommandList *execute[] = {commands[0].Get()};
    queue->ExecuteCommandLists(1, execute);
    check(queue->Signal(fence.Get(), 1));
    require(submitted.submitted_after_execute(fence.Get(), 1));
    const bool pending_not_read = !submitted.read_completed();
    check(gate->Signal(1));
    require(pending_not_read);
    const auto close_handle = [](void *value) { CloseHandle(value); };
    std::unique_ptr<void, decltype(close_handle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), close_handle);
    require(event != nullptr);
    check(fence->SetEventOnCompletion(1, event.get()));
    require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
    const auto &bytes = submitted.read_completed();
    require(bytes && bytes->size() == sizeof(after));
    require(std::memcmp(bytes->data(), after.data(), sizeof(after)) == 0);
    require(std::memcmp(bytes->data(), before.data(), sizeof(before)) != 0);
    // Completion of A's fence cannot grant data to the unsubmitted B.
    require(!unsubmitted.read_completed());
    require(unsubmitted.discard_unsubmitted());
    for (auto &snapshot : pending)
        require(snapshot->discard_unsubmitted());
    require(!unsubmitted.read_completed());
    std::cout << "PASS: GPU values reflect late upload writes; unsubmitted recording stays unread; bounds rejected\n";
}
int main() {
    try {
        for (auto type :
             {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY})
            run_case(type);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
