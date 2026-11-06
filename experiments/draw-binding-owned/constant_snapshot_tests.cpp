// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "constant_capture_driver.hpp"
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
    require(submitted.before_execute());
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
    const auto bytes = submitted.read_completed();
    require(bytes && bytes->size() == sizeof(after));
    require(std::memcmp(bytes->data(), after.data(), sizeof(after)) == 0);
    require(std::memcmp(bytes->data(), before.data(), sizeof(before)) != 0);
    // Completion does not retire a recording: it is legal to execute A again.
    // Its readback allocation must still be alive and capacity still consumed.
    require(!over_capacity.record(device.Get(), commands[1].Get(), source.Get(), 256, sizeof(before)));
    check(gate->Signal(1));
    check(queue->Wait(gate.Get(), 2));
    struct SecondReleaseGate {
        ID3D12Fence *fence;
        ~SecondReleaseGate() { fence->Signal(2); }
    } second_release{gate.Get()};
    require(submitted.before_execute());
    require(!submitted.release_retired()); // Before-execute is not completion.
    queue->ExecuteCommandLists(1, execute);
    check(queue->Signal(fence.Get(), 2));
    require(submitted.submitted_after_execute(fence.Get(), 2));
    require(!submitted.release_retired()); // Latest execution is still blocked.
    // The earlier CPU sample is frozen, even while a later GPU copy is pending.
    require(submitted.read_completed() == bytes);
    check(gate->Signal(2));
    check(fence->SetEventOnCompletion(2, event.get()));
    require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
    check(allocators[0]->Reset());
    check(commands[0]->Reset(allocators[0].Get(), nullptr));
    require(submitted.release_retired()); // Successful reset + latest completion.
    require(submitted.read_completed() == bytes);
    require(!submitted.before_execute());
    // Completion of A's fence cannot grant data to the unsubmitted B.
    require(!unsubmitted.read_completed());
    check(allocators[1]->Reset());
    check(commands[1]->Reset(allocators[1].Get(), nullptr));
    require(unsubmitted.discard_unsubmitted());
    for (auto &snapshot : pending)
        require(snapshot->discard_unsubmitted());
    require(!unsubmitted.read_completed());
    // Exercise the event bridge with real GPU work. The tests call AFTER-
    // present only after native Execute returned; actual addon wiring needs
    // a separate runtime qualification.
    odv::research::ConstantCaptureDriver driver;
    driver.begin_capture();
    const auto a = driver.record(1, device.Get(), commands[0].Get(), source.Get(), 256, sizeof(after));
    const auto b = driver.record(2, device.Get(), commands[1].Get(), source.Get(), 256, sizeof(after));
    require(a != nullptr && b != nullptr);
    require(!driver.record(1, device.Get(), commands[0].Get(), source.Get(), 256, sizeof(after)));
    check(commands[0]->Close());
    check(commands[1]->Close());
    check(queue->Wait(gate.Get(), 3));
    struct ThirdReleaseGate {
        ID3D12Fence *fence;
        ~ThirdReleaseGate() { fence->Signal(3); }
    } third_release{gate.Get()};
    driver.before_execute(1, queue.Get());
    require(driver.read(a).bytes.empty());
    queue->ExecuteCommandLists(1, execute);
    Com<ID3D12CommandQueue> other_queue;
    check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&other_queue)));
    driver.after_present(other_queue.Get());
    std::thread wrong_thread([&] { driver.after_present(queue.Get()); });
    wrong_thread.join();
    require(driver.read(a).bytes.empty());
    driver.after_present(queue.Get());
    require(driver.read(a).bytes.empty()); // Queue still blocked.
    check(gate->Signal(3));
    check(queue->Signal(fence.Get(), 3));
    check(fence->SetEventOnCompletion(3, event.get()));
    require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
    const auto sample = driver.read(a);
    require(std::string_view(sample.state) == "READY" && sample.bytes.size() == sizeof(after));
    require(std::memcmp(sample.bytes.data(), after.data(), sizeof(after)) == 0);
    require(std::string_view(driver.read(b).state) == "UNSUBMITTED");
    // A before-reset event is insufficient. Its old commands can still execute
    // if Reset fails; retain the allocation and track that repeated execution.
    driver.before_reset(1);
    driver.before_execute(1, queue.Get());
    queue->ExecuteCommandLists(1, execute);
    driver.after_present(queue.Get());
    check(queue->Signal(fence.Get(), 4));
    check(fence->SetEventOnCompletion(4, event.get()));
    require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
    check(allocators[0]->Reset());
    check(commands[0]->Reset(allocators[0].Get(), nullptr));
    driver.recording_resumed(1);
    require(driver.read(a).bytes == sample.bytes);
    driver.before_reset(2);
    check(allocators[1]->Reset());
    check(commands[1]->Reset(allocators[1].Get(), nullptr));
    driver.recording_resumed(2);
    require(driver.read(b).bytes.empty());
    driver.begin_capture();
    require(driver.record(1, device.Get(), commands[0].Get(), source.Get(), 256, sizeof(after)) != nullptr);
    commands[0].Reset();
    driver.destroyed(1);
    std::cout << "PASS: late upload writes, repeated execution, retirement, unsubmitted and bounds controls\n";
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
