// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "scoped_scalar_patch.hpp"
#include <array>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <iostream>
#include <memory>
#include <string_view>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
#ifdef ODV_PRIVATE_SCALAR_PATCH
static constexpr bool private_mode = true;
#else
static constexpr bool private_mode = false;
#endif
static void require(bool value) {
    if (!value)
        throw std::runtime_error("scalar_patch_control_failed");
}
static void checked(HRESULT result) { require(SUCCEEDED(result)); }
int main(int argc, char **argv) {
    try {
        Com<ID3D12Debug> debug;
        const bool validation = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        if (validation)
            debug->EnableDebugLayer();
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        const bool hardware = argc == 2 && std::string_view(argv[1]) == "--hardware";
        checked(hardware ? factory->EnumAdapters(0, &adapter) : factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options))) ||
            !options.EnhancedBarriersSupported) {
            std::cout << "NOT_RUN: enhanced barriers unsupported; adapter=" << (hardware ? "hardware" : "WARP") << '\n';
            return 77;
        }
        Com<ID3D12InfoQueue> info;
        if (validation)
            checked(device.As(&info));
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        Com<ID3D12CommandAllocator> allocator;
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList7> commands;
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&commands)));
        const auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = type;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = 4096;
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
        auto source = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        auto independent = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto during = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto original_during = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        std::array<float, 704> expected{};
        for (unsigned i = 0; i < expected.size(); ++i)
            expected[i] = static_cast<float>(i) + 0.25f;
        void *mapped = nullptr;
        const D3D12_RANGE empty{};
        checked(upload->Map(0, &empty, &mapped));
        std::memcpy(mapped, expected.data(), sizeof(expected));
        upload->Unmap(0, nullptr);
        odv::research::KnownBufferStates states;
        odv::research::ScopedScalarPatch capture;
        const auto begin = [&] {
            if constexpr (private_mode)
                return capture.begin_private(device.Get(), commands.Get(), source.Get(), 256, sizeof(expected), 0x4e0,
                                             2.0f, states);
            else
                return capture.begin(device.Get(), commands.Get(), source.Get(), 256 + 0x4e0, 2.0f, states);
        };
        D3D12_BUFFER_BARRIER barrier{};
        barrier.pResource = source.Get();
        barrier.Size = UINT64_MAX;
        D3D12_BARRIER_GROUP group{};
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = 1;
        group.pBufferBarriers = &barrier;
        {
            // Adversarial metadata controls do not record invalid GPU work.
            odv::research::KnownBufferStates probe;
            barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
            barrier.AccessAfter = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
            barrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
            barrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
            probe.observe_enhanced(1, &group);
            require(!probe.enhanced_constant_scope(source.Get())); // Missing BEGIN.
            barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
            barrier.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
            probe.observe_enhanced(1, &group);
            barrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
            barrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
            barrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
            probe.observe_enhanced(1, &group);
            require(!probe.enhanced_constant_scope(source.Get())); // Mismatched END.
            barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
            barrier.AccessAfter = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
            barrier.Offset = 256;
            probe.observe_enhanced(1, &group);
            require(!probe.enhanced_constant_scope(source.Get())); // Partial coverage.
            barrier.Offset = 0;
            probe.observe_enhanced(1, &group);
            require(probe.enhanced_constant_scope(source.Get()));
            D3D12_RESOURCE_BARRIER legacy{};
            legacy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            legacy.Transition = {source.Get(), 0, D3D12_RESOURCE_STATE_COMMON,
                                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
            probe.observe(1, &legacy);
            require(!probe.enhanced_constant_scope(source.Get()));
            require(probe.constant_read_state(source.Get()).has_value());
            probe.observe_enhanced(1, &group);
            require(!probe.constant_read_state(source.Get()));
            require(probe.enhanced_constant_scope(source.Get()));
            probe.forget(source.Get());
            require(!probe.enhanced_constant_scope(source.Get()));
            probe.observe_enhanced(1, &group);
            D3D12_GLOBAL_BARRIER global{};
            D3D12_BARRIER_GROUP global_group{};
            global_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
            global_group.NumBarriers = 1;
            global_group.pGlobalBarriers = &global;
            probe.observe_enhanced(1, &global_group);
            require(!probe.enhanced_constant_scope(source.Get()));
            probe.observe_enhanced(1, &group);
            probe.reset();
            require(!probe.enhanced_constant_scope(source.Get()));
        }
        const auto record = [&] {
            commands->Barrier(1, &group);
            states.observe_enhanced(1, &group);
        };
        require(!begin());
        barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
        record();
        commands->CopyBufferRegion(source.Get(), 256, upload.Get(), 0, sizeof(expected));
        barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
        record();
        require(!states.enhanced_constant_scope(source.Get()));
        require(!begin());
        barrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
        record();
        require(states.enhanced_constant_scope(source.Get()));
        require(!states.constant_read_state(source.Get())); // Never reinterpret enhanced access as legacy state.
        require(begin());
        if constexpr (private_mode) {
            require(capture.private_buffer() && capture.private_buffer() != source.Get());
            require(capture.private_gpu_address() != source->GetGPUVirtualAddress());
            barrier.pResource = capture.private_buffer();
        }
        require(!capture.before_execute()); // Missing after-draw restoration.
        barrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        record();
        commands->CopyBufferRegion(during.Get(), 0, private_mode ? capture.private_buffer() : source.Get(),
                                   private_mode ? 0 : 256, sizeof(expected));
        barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
        record();
        if constexpr (private_mode) {
            barrier.pResource = source.Get();
            barrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
            barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
            barrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
            barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            record();
            commands->CopyBufferRegion(original_during.Get(), 0, source.Get(), 256, sizeof(expected));
            barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
            barrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
            barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            barrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
            record();
        }
        require(capture.end(commands.Get()));
        require(!capture.end(commands.Get())); // Never restore twice.
        barrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        record();
        commands->CopyBufferRegion(independent.Get(), 0, source.Get(), 256, sizeof(expected));
        checked(commands->Close());
        Com<ID3D12Fence> fence, gate;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
        struct Unblock {
            ID3D12Fence *gate;
            ~Unblock() { gate->Signal(1); }
        } unblock{gate.Get()};
        checked(queue->Wait(gate.Get(), 1));
        require(capture.before_execute());
        ID3D12CommandList *execute[] = {commands.Get()};
        queue->ExecuteCommandLists(1, execute);
        checked(queue->Signal(fence.Get(), 1));
        require(capture.submitted_after_execute(fence.Get(), 1));
        require(!capture.release_retired()); // Blocked fence prevents release even with caller retirement proof.
        checked(gate->Signal(1));
        const auto close = [](void *value) { CloseHandle(value); };
        std::unique_ptr<void, decltype(close)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), close);
        require(event != nullptr);
        checked(fence->SetEventOnCompletion(1, event.get()));
        require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0);
        const D3D12_RANGE changed_range{0, sizeof(expected)};
        checked(during->Map(0, &changed_range, &mapped));
        auto changed = expected;
        changed[0x4e0 / 4] = 2.0f;
        const bool patched = std::memcmp(mapped, changed.data(), sizeof(changed)) == 0;
        during->Unmap(0, &empty);
        require(patched);
        if constexpr (private_mode) {
            checked(original_during->Map(0, &changed_range, &mapped));
            const bool original_never_changed = std::memcmp(mapped, expected.data(), sizeof(expected)) == 0;
            original_during->Unmap(0, &empty);
            require(original_never_changed);
        }
        const D3D12_RANGE range{0, sizeof(expected)};
        checked(independent->Map(0, &range, &mapped));
        const bool unchanged = std::memcmp(mapped, expected.data(), sizeof(expected)) == 0;
        independent->Unmap(0, &empty);
        require(unchanged);
        checked(allocator->Reset());
        checked(commands->Reset(allocator.Get(), nullptr));
        require(capture.release_retired());
        odv::research::DeferredResourceRelease::drain();
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
        std::cout << "PASS: "
                  << (private_mode ? "private-copy patch, original unchanged during/after" : "owned in-place patch")
                  << ", exact 2816-byte controls, split/fence gating; adapter=" << (hardware ? "hardware" : "WARP")
                  << "; debug=" << (validation ? "ENABLED" : "UNAVAILABLE") << "; game=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
