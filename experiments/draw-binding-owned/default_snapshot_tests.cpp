// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "constant_capture_driver.hpp"
#include <array>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <iostream>
#include <memory>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static void require(bool value) {
    if (!value)
        throw std::runtime_error("default_snapshot_control_failed");
}
static void checked(HRESULT result) { require(SUCCEEDED(result)); }

int main() {
    try {
        Com<ID3D12Debug> debug;
        const bool validation = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        if (validation)
            debug->EnableDebugLayer();
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        checked(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        Com<ID3D12InfoQueue> info;
        if (validation)
            checked(device.As(&info));
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        Com<ID3D12CommandAllocator> allocator;
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList> commands;
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&commands)));
        const auto buffer = [&](D3D12_HEAP_TYPE heap_type, D3D12_RESOURCE_STATES state) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = heap_type;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = 1024;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Com<ID3D12Resource> result;
            checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                    IID_PPV_ARGS(&result)));
            return result;
        };
        auto upload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto source = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        auto independent = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        {
            // Metadata-only controls; incomplete barriers are not recorded on
            // a native command list. All identities are owned real resources.
            odv::research::KnownBufferStates probe;
            require(std::strcmp(probe.diagnostic(source.Get()), "NO_CURRENT_RESOURCE_OBSERVATION") == 0);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = {source.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST,
                                  D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
            probe.observe(1, &barrier);
            require(!probe.constant_read_state(source.Get())); // Missing BEGIN.
            require(std::strcmp(probe.diagnostic(source.Get()), "RANGE_OR_TRANSITION_UNVERIFIED") == 0);
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            probe.observe(1, &barrier);
            require(probe.constant_read_state(source.Get()).has_value());
            require(std::strcmp(probe.diagnostic(source.Get()), "LEGACY_CONSTANT_READ") == 0);
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
            probe.observe(1, &barrier);
            require(!probe.constant_read_state(source.Get()));
            require(std::strcmp(probe.diagnostic(source.Get()), "SPLIT_INCOMPLETE") == 0);
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
            probe.observe(1, &barrier);
            require(!probe.constant_read_state(source.Get())); // Mismatched END.
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            for (auto state : {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE}) {
                barrier.Transition.StateAfter = state;
                probe.observe(1, &barrier);
                require(!probe.constant_read_state(source.Get()));
            }
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
            probe.observe(1, &barrier);
            require(probe.constant_read_state(source.Get()).has_value());
            probe.forget(source.Get()); // Destruction or unsupported barrier API.
            require(!probe.constant_read_state(source.Get()));
            probe.observe(1, &barrier);
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            barrier.Aliasing = {nullptr, nullptr};
            probe.observe(1, &barrier);
            require(!probe.constant_read_state(source.Get()));
        }
        {
            // Exhaustion must stay distinguishable from a missing observation,
            // and cannot recover copy proof until the recording resets.
            odv::research::KnownBufferStates probe;
            std::array<Com<ID3D12Resource>, 129> resources;
            for (auto &resource : resources) {
                resource = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
                D3D12_RESOURCE_BARRIER b{};
                b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition = {resource.Get(), 0, D3D12_RESOURCE_STATE_COMMON,
                                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
                probe.observe(1, &b); // Metadata only; no native transition.
            }
            require(std::strcmp(probe.diagnostic(resources.front().Get()), "CAPACITY_EXHAUSTED") == 0);
            require(probe.tracked_resources() == 0);
            require(!probe.constant_read_state(resources.back().Get()));
            probe.invalidate_all();
            require(std::strcmp(probe.diagnostic(source.Get()), "CAPACITY_EXHAUSTED") == 0);
            probe.reset();
            require(std::strcmp(probe.diagnostic(source.Get()), "NO_CURRENT_RESOURCE_OBSERVATION") == 0);
        }
        std::array<float, 16> initial{}, expected{};
        for (unsigned i = 0; i < expected.size(); ++i) {
            initial[i] = static_cast<float>(i);
            expected[i] = static_cast<float>(i) + 100;
        }
        void *mapped = nullptr;
        const D3D12_RANGE empty{};
        checked(upload->Map(0, &empty, &mapped));
        std::memcpy(mapped, initial.data(), sizeof(initial));
        odv::research::KnownBufferStates states;
        const auto transition = [&](D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                                    D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = flags;
            barrier.Transition = {source.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
            commands->ResourceBarrier(1, &barrier);
            states.observe(1, &barrier); // Exact native flags, after recording.
        };
        Com<ID3D12Fence> fence, gate;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
        const auto close = [](void *handle) { CloseHandle(handle); };
        std::unique_ptr<void, decltype(close)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), close);
        require(event != nullptr);
        odv::research::ConstantCaptureDriver driver;
        driver.begin_capture();
        for (UINT64 pass = 1; pass <= 2; ++pass) {
            odv::research::ConstantSnapshot capture;
            odv::research::ConstantCaptureDriver::Ticket ticket;
            require(
                !driver.record_default(123, device.Get(), commands.Get(), source.Get(), 64, sizeof(expected), states));
            require(!capture.record_default(device.Get(), commands.Get(), source.Get(), 64, sizeof(expected), states));
            if (pass == 1) {
                commands->CopyBufferRegion(source.Get(), 64, upload.Get(), 0, sizeof(expected));
                transition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                           D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
                require(!states.constant_read_state(source.Get()));
                require(
                    !capture.record_default(device.Get(), commands.Get(), source.Get(), 64, sizeof(expected), states));
                transition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                           D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
            } else {
                // Buffers decay to COMMON after the previous execution batch.
                transition(D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_GENERIC_READ);
            }
            const auto expected_state =
                pass == 1 ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : D3D12_RESOURCE_STATE_GENERIC_READ;
            require(states.constant_read_state(source.Get()) == expected_state);
            require(!capture.record_default(device.Get(), commands.Get(), upload.Get(), 0, sizeof(expected), states));
            require(
                !capture.record_default(device.Get(), commands.Get(), source.Get(), 1000, sizeof(expected), states));
            require(capture.record_default(device.Get(), commands.Get(), source.Get(), 64, sizeof(expected), states));
            if (pass == 2) {
                odv::research::ConstantCaptureDriver::RecordStatus status;
                ticket = driver.record_default(123, device.Get(), commands.Get(), source.Get(), 64, sizeof(expected),
                                               states, &status);
                require(ticket && status.source.supported && status.source.heap_type == D3D12_HEAP_TYPE_DEFAULT);
                require(!driver.record_default(123, device.Get(), commands.Get(), source.Get(), 64, sizeof(expected),
                                               states)); // Duplicate range.
            }
            // This before-state assertion is checked by the D3D12 debug layer:
            // the capture must have restored exactly the application's state.
            transition(expected_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->CopyBufferRegion(independent.Get(), 0, source.Get(), 64, sizeof(expected));
            checked(commands->Close());
            if (pass == 1) {
                // Late CPU writes must reach DEFAULT through actual GPU work.
                std::memcpy(mapped, expected.data(), sizeof(expected));
                upload->Unmap(0, nullptr);
            }
            struct Unblock {
                ID3D12Fence *fence;
                UINT64 value;
                ~Unblock() { fence->Signal(value); }
            } unblock{gate.Get(), pass};
            checked(queue->Wait(gate.Get(), pass));
            require(capture.before_execute());
            driver.before_execute(123, queue.Get());
            ID3D12CommandList *execute[] = {commands.Get()};
            queue->ExecuteCommandLists(1, execute);
            checked(queue->Signal(fence.Get(), pass * 2));
            require(capture.submitted_after_execute(fence.Get(), pass * 2));
            driver.after_execute(999, queue.Get()); // A different recording grants nothing.
            require(driver.read(ticket).bytes.empty());
            driver.after_execute(123, queue.Get()); // Actual native Execute has returned; no Present needed.
            checked(queue->Signal(fence.Get(), pass * 2 + 1));
            require(!capture.read_completed());
            require(driver.read(ticket).bytes.empty());
            checked(gate->Signal(pass));
            checked(fence->SetEventOnCompletion(pass * 2 + 1, event.get()));
            require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
            const auto &data = capture.read_completed();
            require(data && data->size() == sizeof(expected) &&
                    std::memcmp(data->data(), expected.data(), sizeof(expected)) == 0);
            if (ticket) {
                const auto copied = driver.read(ticket);
                require(copied.bytes.size() == sizeof(expected) &&
                        std::memcmp(copied.bytes.data(), expected.data(), sizeof(expected)) == 0);
            }
            const D3D12_RANGE range{0, sizeof(expected)};
            void *pixels = nullptr;
            checked(independent->Map(0, &range, &pixels));
            const bool unchanged = std::memcmp(pixels, expected.data(), sizeof(expected)) == 0;
            independent->Unmap(0, &empty);
            require(unchanged);
            driver.before_reset(123);
            checked(allocator->Reset());
            checked(commands->Reset(allocator.Get(), nullptr));
            driver.recording_resumed(123);
            states.reset();
            require(!states.constant_read_state(source.Get()));
            require(capture.release_retired());
            odv::research::DeferredResourceRelease::drain();
        }
        checked(commands->Close());
        if (info)
            for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T size = 0;
                checked(info->GetMessage(i, nullptr, &size));
                auto storage = std::make_unique<std::byte[]>(size);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.get());
                checked(info->GetMessage(i, message, &size));
                require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                        message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION);
            }
        std::cout
            << "PASS: known DEFAULT states, split barrier rejection, restored app state, completed GPU bytes; debug="
            << (validation ? "ENABLED" : "UNAVAILABLE") << "; game=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
