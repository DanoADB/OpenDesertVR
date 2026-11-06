// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "constant_capture_driver.hpp"
#include "owned_constant_copy.hpp"
#include "upload_input_observer.hpp"
#include <array>
#include <dxgi1_4.h>
#include <iostream>
#include <memory>
#include <string_view>

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
    const auto default_info = ConstantSnapshot::inspect_source(gpu_only.Get(), 0, sizeof(before));
    require(!default_info.supported && default_info.heap_type == D3D12_HEAP_TYPE_DEFAULT &&
            default_info.width == desc.Width && std::string_view(default_info.state) == "HEAP_NOT_UPLOAD");
    const auto upload_info = ConstantSnapshot::inspect_source(source.Get(), 256, sizeof(before));
    require(upload_info.supported && upload_info.heap_type == D3D12_HEAP_TYPE_UPLOAD &&
            std::string_view(upload_info.state) == "ELIGIBLE_UPLOAD");
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
    odv::research::ConstantCaptureDriver::RecordStatus record_status;
    require(!driver.record(1, device.Get(), commands[0].Get(), gpu_only.Get(), 256, sizeof(after), &record_status));
    require(std::string_view(record_status.state) == "INELIGIBLE_SOURCE" &&
            std::string_view(record_status.source.state) == "HEAP_NOT_UPLOAD");
    const auto a = driver.record(1, device.Get(), commands[0].Get(), source.Get(), 256, sizeof(after), &record_status);
    require(std::string_view(record_status.state) == "RECORDED" && record_status.source.supported);
    const auto b = driver.record(2, device.Get(), commands[1].Get(), source.Get(), 256, sizeof(after));
    require(a != nullptr && b != nullptr);
    require(!driver.record(1, device.Get(), commands[0].Get(), source.Get(), 256, sizeof(after), &record_status));
    require(std::string_view(record_status.state) == "DUPLICATE_SOURCE_RANGE");
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
    // A copy queue never presents. An exclusively owned copy list can obtain
    // its own causally ordered fence without relying on an app AFTER-present.
    // This is the next upload-input observation route, not DEFAULT readback.
    check(queue->Wait(gate.Get(), 4));
    struct FourthReleaseGate {
        ID3D12Fence *fence;
        ~FourthReleaseGate() { fence->Signal(4); }
    } fourth_release{gate.Get()};
    odv::research::OwnedConstantCopy owned_copy;
    require(owned_copy.submit(queue.Get(), source.Get(), 256, sizeof(after)));
    require(!owned_copy.read_completed());
    std::array<float, 16> upload_input{};
    for (unsigned i = 0; i < upload_input.size(); ++i)
        upload_input[i] = static_cast<float>(200 + i);
    check(source->Map(0, &empty, &mapped));
    std::memcpy(static_cast<std::byte *>(mapped) + 256, upload_input.data(), sizeof(upload_input));
    source->Unmap(0, nullptr);
    check(gate->Signal(4));
    check(queue->Signal(fence.Get(), 5));
    check(fence->SetEventOnCompletion(5, event.get()));
    require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
    const auto &upload_bytes = owned_copy.read_completed();
    require(upload_bytes && upload_bytes->size() == sizeof(upload_input));
    require(std::memcmp(upload_bytes->data(), upload_input.data(), sizeof(upload_input)) == 0);
    require(!owned_copy.submit(queue.Get(), source.Get(), 256, sizeof(after)));
    odv::research::UploadInputObserver uploads;
    uploads.begin_capture();
    require(!uploads.register_target(source.Get(), 16, 16)); // DEFAULT candidates only.
    const auto target_a = uploads.register_target(gpu_only.Get(), 16, 16);
    const auto target_b = uploads.register_target(gpu_only.Get(), 128, 16);
    require(target_a && target_b && !uploads.register_target(gpu_only.Get(), 16, 16));
    uploads.observe_copy(10, source.Get(), 256, gpu_only.Get(), 0, 8);
    require(uploads.read(target_a).bytes.empty()); // Partial copy cannot cover target.
    uploads.observe_copy(10, source.Get(), UINT64_MAX - 8, gpu_only.Get(), 0, 64);
    require(uploads.read(target_a).bytes.empty()); // Source-offset overflow rejected.
    uploads.observe_copy(10, gpu_only.Get(), 256, gpu_only.Get(), 0, 64);
    require(std::string_view(uploads.read(target_a).state) == "UPLOAD_SOURCE_INELIGIBLE");
    std::array<Com<ID3D12CommandAllocator>, 2> app_allocators;
    std::array<Com<ID3D12GraphicsCommandList>, 2> app_lists;
    for (unsigned i = 0; i < 2; ++i) {
        check(device->CreateCommandAllocator(queue_type, IID_PPV_ARGS(&app_allocators[i])));
        check(device->CreateCommandList(0, queue_type, app_allocators[i].Get(), nullptr, IID_PPV_ARGS(&app_lists[i])));
    }
    const auto transition = [&](ID3D12GraphicsCommandList *list, D3D12_RESOURCE_STATES before_state,
                                D3D12_RESOURCE_STATES after_state) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {gpu_only.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before_state, after_state};
        list->ResourceBarrier(1, &barrier);
    };
    D3D12_HEAP_PROPERTIES cpu_read{};
    cpu_read.Type = D3D12_HEAP_TYPE_READBACK;
    auto read_desc = desc;
    read_desc.Width = 16;
    Com<ID3D12Resource> default_readback;
    check(device->CreateCommittedResource(&cpu_read, D3D12_HEAP_FLAG_NONE, &read_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&default_readback)));
    transition(app_lists[0].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    uploads.observe_copy(10, source.Get(), 256, gpu_only.Get(), 0, 64);
    app_lists[0]->CopyBufferRegion(gpu_only.Get(), 0, source.Get(), 256, 64);
    transition(app_lists[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    app_lists[0]->CopyBufferRegion(default_readback.Get(), 0, gpu_only.Get(), 16, 16);
    check(app_lists[0]->Close());
    transition(app_lists[1].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    uploads.observe_copy(11, source.Get(), 256, gpu_only.Get(), 128, 64);
    app_lists[1]->CopyBufferRegion(gpu_only.Get(), 128, source.Get(), 256, 64);
    transition(app_lists[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check(app_lists[1]->Close());
    require(std::string_view(uploads.read(target_b).state) == "UPLOAD_MATCHED_NOT_SUBMITTED");
    check(queue->Wait(gate.Get(), 5));
    struct FifthReleaseGate {
        ID3D12Fence *fence;
        ~FifthReleaseGate() { fence->Signal(5); }
    } fifth_release{gate.Get()};
    uploads.before_execute(10, queue.Get());
    ID3D12CommandList *app_execute[] = {app_lists[0].Get()};
    queue->ExecuteCommandLists(1, app_execute);
    require(uploads.read(target_a).bytes.empty());
    check(gate->Signal(5));
    check(queue->Signal(fence.Get(), 6));
    check(fence->SetEventOnCompletion(6, event.get()));
    require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
    const auto observed_input = uploads.read(target_a);
    require(std::string_view(observed_input.state) == "READY_UPLOAD_INPUT" && observed_input.bytes.size() == 16);
    require(std::memcmp(observed_input.bytes.data(), upload_input.data() + 4, 16) == 0);
    const D3D12_RANGE mapped_range{0, 16};
    check(default_readback->Map(0, &mapped_range, &mapped));
    const bool equals_default = std::memcmp(mapped, observed_input.bytes.data(), 16) == 0;
    default_readback->Unmap(0, &empty);
    require(equals_default); // Independently read actual application DEFAULT result.
    require(uploads.read(target_b).bytes.empty());
    uploads.before_reset(11);
    check(app_allocators[1]->Reset());
    check(app_lists[1]->Reset(app_allocators[1].Get(), nullptr));
    uploads.recording_resumed(11); // Next callback is Close on an EMPTY new recording.
    check(app_lists[1]->Close());
    uploads.before_execute(11, queue.Get());
    app_execute[0] = app_lists[1].Get();
    queue->ExecuteCommandLists(1, app_execute);
    check(queue->Signal(fence.Get(), 7));
    check(fence->SetEventOnCompletion(7, event.get()));
    require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
    require(uploads.read(target_b).bytes.empty()); // Empty resubmission cannot grant stale data.
    uploads.release_device(device.Get());
    require(std::string_view(uploads.read(target_a).state) == "DEVICE_CLOSED");
    require(std::string_view(uploads.read(target_b).state) == "DEVICE_CLOSED");
    require(uploads.read(target_a).bytes.empty() && uploads.read(target_b).bytes.empty());
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
