// SPDX-License-Identifier: MIT
// Owned-only feasibility control. No game integration or generic state restore.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "vertex_constant_stream.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
static void checked(HRESULT value) {
    if (FAILED(value)) {
        std::cerr << "HRESULT=0x" << std::hex << static_cast<unsigned long>(value) << std::dec << '\n';
        throw std::runtime_error("native_call_failed");
    }
}
static void transition(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands->ResourceBarrier(1, &barrier);
}

int main(int argc, char **argv) {
    try {
        constexpr UINT bytes = 2816, vertices = bytes / 16, source_bytes = 4096, source_offset = 256;
        const bool hardware = argc == 2 && std::string_view(argv[1]) == "--hardware";
        require(argc == 1 || hardware, "invalid_arguments");
        Com<ID3D12Debug> debug;
        const bool validation = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        if (validation)
            debug->EnableDebugLayer();
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        if (!hardware)
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
        const auto buffer = [&](UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = type;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = size;
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
        auto upload = buffer(source_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto source = buffer(source_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        auto independent = buffer(source_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto zeros = buffer(8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto predicate = buffer(8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        std::array<std::uint32_t, source_bytes / 4> expected{};
        constexpr std::array<std::uint32_t, 8> patterns{0, 0x7fc01234, 0xffffffff, 0x80000000,
                                                        1, 0x7f800000, 0xff800000, 0x3f800000};
        for (size_t i = 0; i < expected.size(); ++i)
            expected[i] = patterns[i % patterns.size()] ^ (i % 2 ? 0 : static_cast<std::uint32_t>(i));
        void *mapped = nullptr;
        const D3D12_RANGE empty{};
        checked(upload->Map(0, &empty, &mapped));
        std::memcpy(mapped, expected.data(), source_bytes);
        upload->Unmap(0, nullptr);
        checked(zeros->Map(0, &empty, &mapped));
        std::memset(mapped, 0, 8);
        zeros->Unmap(0, nullptr);
        commands->CopyBufferRegion(source.Get(), 0, upload.Get(), 0, source_bytes);
        commands->CopyBufferRegion(predicate.Get(), 0, zeros.Get(), 0, 8);
        transition(commands.Get(), source.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        transition(commands.Get(), predicate.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION);

        constexpr char shader[] = R"(
cbuffer Input : register(b0) { uint4 words[176]; };
struct Output { float4 position : SV_Position; uint4 data : DATA; };
Output main(uint id : SV_VertexID) {
    Output o; o.position = float4(0, 0, 0, 1); o.data = words[id]; return o;
})";
        Com<ID3DBlob> bytecode, errors;
        const auto compile =
            D3DCompile(shader, sizeof(shader) - 1, "owned-vertex-stream", nullptr, nullptr, "main", "vs_5_0",
                       D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
        if (FAILED(compile) && errors)
            std::cerr.write(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
        checked(compile);
        D3D12_ROOT_PARAMETER root{};
        root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root.Descriptor = {0, 0};
        root.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 1;
        root_desc.pParameters = &root;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
        Com<ID3DBlob> serialized;
        checked(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
        Com<ID3D12RootSignature> signature;
        checked(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                            IID_PPV_ARGS(&signature)));
        const D3D12_SO_DECLARATION_ENTRY declaration{0, "DATA", 0, 0, 4, 0};
        const UINT stride = 16;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = signature.Get();
        pipeline_desc.VS = {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
        pipeline_desc.StreamOutput = {&declaration, 1, &stride, 1, D3D12_SO_NO_RASTERIZED_STREAM};
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        pipeline_desc.SampleDesc.Count = 1;
        Com<ID3D12PipelineState> pipeline;
        checked(device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)));
        Com<ID3D12Fence> fence, gate;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
        const auto close = [](void *handle) { CloseHandle(handle); };
        std::unique_ptr<void, decltype(close)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), close);
        require(event != nullptr, "event_creation_failed");
        struct Unblock {
            ID3D12Fence *gate;
            ~Unblock() { gate->Signal(UINT64_MAX - 1); }
        } unblock{gate.Get()};
        for (UINT64 pass = 1; pass <= 4; ++pass) {
            // Each recording owns separate outputs; never reuse a stale SO counter.
            auto stream = buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_STREAM_OUT);
            auto counter = buffer(8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            constexpr UINT counter_offset = bytes + sizeof(D3D12_QUERY_DATA_SO_STATISTICS);
            auto readback = buffer(counter_offset + 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            Com<ID3D12QueryHeap> query;
            D3D12_QUERY_HEAP_DESC query_desc{D3D12_QUERY_HEAP_TYPE_SO_STATISTICS, 1, 0};
            checked(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&query)));
            if (pass != 1) {
                // Buffers decay to COMMON between completed execution batches.
                transition(commands.Get(), source.Get(), D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                transition(commands.Get(), predicate.Get(), D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_PREDICATION);
            }
            // Sentinel makes a skipped copy observably different from a valid sample.
            commands->CopyBufferRegion(readback.Get(), 0, upload.Get(), 0, bytes);
            commands->CopyBufferRegion(counter.Get(), 0, zeros.Get(), 0, 8);
            transition(commands.Get(), counter.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);
            if (pass == 2 || pass == 3)
                commands->SetPredication(predicate.Get(), 0,
                                         pass == 2 ? D3D12_PREDICATION_OP_EQUAL_ZERO
                                                   : D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
            commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
            commands->SetGraphicsRootSignature(signature.Get());
            commands->SetPipelineState(pipeline.Get());
            commands->SetGraphicsRootConstantBufferView(0, source->GetGPUVirtualAddress() + source_offset);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
            const UINT capacity = pass == 4 ? bytes - 16 : bytes;
            const D3D12_STREAM_OUTPUT_BUFFER_VIEW view{stream->GetGPUVirtualAddress(), capacity,
                                                       counter->GetGPUVirtualAddress()};
            commands->SOSetTargets(0, 1, &view);
            commands->BeginQuery(query.Get(), D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);
            commands->DrawInstanced(vertices, 1, 0, 0);
            commands->EndQuery(query.Get(), D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);
            const std::array<D3D12_STREAM_OUTPUT_BUFFER_VIEW, 4> unbound{};
            commands->SOSetTargets(0, static_cast<UINT>(unbound.size()), unbound.data());
            transition(commands.Get(), stream.Get(), D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->CopyBufferRegion(readback.Get(), 0, stream.Get(), 0, bytes);
            transition(commands.Get(), counter.Get(), D3D12_RESOURCE_STATE_STREAM_OUT,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->CopyBufferRegion(readback.Get(), counter_offset, counter.Get(), 0, 8);
            // Query resolution is unpredicated, even if the draw/copy were skipped.
            commands->ResolveQueryData(query.Get(), D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0, 1, readback.Get(),
                                       bytes);
            odv::research::VertexConstantStream component;
            const odv::research::VertexConstantStream::Context context{true, true, true, true, true, true};
            const auto restore = [&]() noexcept {
                commands->SetGraphicsRootSignature(signature.Get());
                commands->SetPipelineState(pipeline.Get());
                commands->SetGraphicsRootConstantBufferView(0, source->GetGPUVirtualAddress() + source_offset);
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
                commands->SOSetTargets(0, static_cast<UINT>(unbound.size()), unbound.data());
            };
            require(!component.record(device.Get(), commands.Get(), source.Get(), source_offset + 1, context, restore),
                    "misaligned_component_source_accepted");
            require(component.record(device.Get(), commands.Get(), source.Get(), source_offset, context, restore),
                    "component_record_failed");
            require(component.read().bytes.empty(), "component_unsubmitted_exposed_bytes");
            commands->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            // Only this independent verification transitions the original source.
            transition(commands.Get(), source.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->CopyBufferRegion(independent.Get(), 0, source.Get(), 0, source_bytes);
            checked(commands->Close());
            checked(queue->Wait(gate.Get(), pass));
            require(component.before_execute(), "component_before_execute_failed");
            ID3D12CommandList *execute[] = {commands.Get()};
            queue->ExecuteCommandLists(1, execute);
            checked(queue->Signal(fence.Get(), pass));
            require(component.submitted_after_execute(fence.Get(), pass), "component_submission_failed");
            require(fence->GetCompletedValue() < pass, "blocked_fence_not_pending");
            require(component.read().bytes.empty() && !component.release_retired(),
                    "component_pending_fence_exposed_data_or_released_resources");
            // No output is mapped before the actual covering fence completes.
            checked(gate->Signal(pass));
            checked(fence->SetEventOnCompletion(pass, event.get()));
            require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0, "gpu_timeout");
            const auto sampled = component.read();
            if (pass == 2)
                require(std::strcmp(sampled.state, "SO_NOT_EXECUTED") == 0 && sampled.bytes.empty(),
                        "component_skipped_draw_accepted");
            else
                require(std::strcmp(sampled.state, "READY") == 0 && sampled.bytes.size() == bytes &&
                            std::memcmp(sampled.bytes.data(),
                                        reinterpret_cast<const std::byte *>(expected.data()) + source_offset,
                                        bytes) == 0,
                        "component_sample_not_exact");
            const D3D12_RANGE range{0, counter_offset + 8};
            checked(readback->Map(0, &range, &mapped));
            D3D12_QUERY_DATA_SO_STATISTICS statistics{};
            std::memcpy(&statistics, static_cast<const std::byte *>(mapped) + bytes, sizeof(statistics));
            UINT64 filled_bytes = 0;
            std::memcpy(&filled_bytes, static_cast<const std::byte *>(mapped) + counter_offset, sizeof(filled_bytes));
            const bool executed = pass != 2;
            const bool valid = statistics.NumPrimitivesWritten == vertices &&
                               statistics.PrimitivesStorageNeeded == vertices && filled_bytes == bytes;
            require(valid == (executed && pass != 4), "unexpected_stream_statistics");
            if (valid)
                require(std::memcmp(mapped, reinterpret_cast<const std::byte *>(expected.data()) + source_offset,
                                    bytes) == 0,
                        "vertex_read_not_bit_exact");
            else if (!executed) {
                require(statistics.NumPrimitivesWritten == 0 && statistics.PrimitivesStorageNeeded == 0,
                        "skipped_draw_nonzero_statistics");
                require(std::memcmp(mapped, expected.data(), bytes) == 0, "skipped_copy_modified_sentinel");
            } else {
                require(statistics.NumPrimitivesWritten == vertices - 1 &&
                            statistics.PrimitivesStorageNeeded == vertices && filled_bytes == bytes - 16,
                        "incomplete_output_not_rejected");
            }
            readback->Unmap(0, &empty);
            const D3D12_RANGE original_range{0, source_bytes};
            checked(independent->Map(0, &original_range, &mapped));
            require(std::memcmp(mapped, expected.data(), source_bytes) == 0, "original_source_modified");
            independent->Unmap(0, &empty);
            // Retire the recording before any referenced resources are destroyed.
            checked(allocator->Reset());
            checked(commands->Reset(allocator.Get(), nullptr));
            require(component.release_retired(), "component_retirement_failed");
            odv::research::DeferredResourceRelease::drain();
        }
        {
            // Re-execute the SAME recording; all owned buffers must return to
            // their initial state, with a new covering fence for each execution.
            odv::research::VertexConstantStream repeated;
            const odv::research::VertexConstantStream::Context context{true, true, true, true, true, true};
            const auto restore = [&]() noexcept {
                commands->SetGraphicsRootSignature(signature.Get());
                commands->SetPipelineState(pipeline.Get());
                commands->SetGraphicsRootConstantBufferView(0, source->GetGPUVirtualAddress() + source_offset);
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
                const std::array<D3D12_STREAM_OUTPUT_BUFFER_VIEW, 4> unbound{};
                commands->SOSetTargets(0, static_cast<UINT>(unbound.size()), unbound.data());
            };
            for (UINT rejected = 0; rejected < 6; ++rejected) {
                auto missing = context;
                switch (rejected) {
                case 0:
                    missing.active_vertex_cbv = false;
                    break;
                case 1:
                    missing.outside_render_pass = false;
                    break;
                case 2:
                    missing.no_active_app_query = false;
                    break;
                case 3:
                    missing.complete_replay_prepared = false;
                    break;
                case 4:
                    missing.stream_output_bindings_known = false;
                    break;
                default:
                    missing.unprotected = false;
                    break;
                }
                require(!repeated.record(device.Get(), commands.Get(), source.Get(), source_offset, missing, restore),
                        "incomplete_context_accepted");
            }
            transition(commands.Get(), source.Get(), D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            require(repeated.record(device.Get(), commands.Get(), source.Get(), source_offset, context, restore),
                    "repeated_record_failed");
            transition(commands.Get(), source.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                       D3D12_RESOURCE_STATE_COMMON);
            checked(commands->Close());
            for (UINT64 execution = 5; execution <= 6; ++execution) {
                checked(queue->Wait(gate.Get(), execution));
                require(repeated.before_execute(), "repeated_before_execute_failed");
                ID3D12CommandList *execute[] = {commands.Get()};
                queue->ExecuteCommandLists(1, execute);
                checked(queue->Signal(fence.Get(), execution));
                require(repeated.submitted_after_execute(fence.Get(), execution), "repeated_submission_failed");
                require(!repeated.release_retired(), "prior_fence_released_new_execution");
                checked(gate->Signal(execution));
                checked(fence->SetEventOnCompletion(execution, event.get()));
                require(WaitForSingleObject(event.get(), 5000) == WAIT_OBJECT_0, "repeat_gpu_timeout");
                const auto data = repeated.read();
                require(std::strcmp(data.state, "READY") == 0 && data.bytes.size() == bytes &&
                            std::memcmp(data.bytes.data(),
                                        reinterpret_cast<const std::byte *>(expected.data()) + source_offset,
                                        bytes) == 0,
                        "repeated_sample_changed");
            }
            checked(allocator->Reset());
            checked(commands->Reset(allocator.Get(), nullptr));
            require(repeated.release_retired(), "repeat_retirement_failed");
            odv::research::DeferredResourceRelease::drain();
            // A never-submitted recording must release only after retirement.
            odv::research::VertexConstantStream abandoned;
            require(abandoned.record(device.Get(), commands.Get(), source.Get(), source_offset, context, restore),
                    "unsubmitted_record_failed");
            checked(commands->Close());
            require(abandoned.read().bytes.empty() && !abandoned.release_retired(),
                    "unsubmitted_capture_granted_completion");
            checked(commands->Reset(allocator.Get(), nullptr));
            require(abandoned.discard_unsubmitted(), "unsubmitted_retirement_failed");
            odv::research::DeferredResourceRelease::drain();
        }
        checked(commands->Close());
        if (info)
            for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T size = 0;
                checked(info->GetMessage(i, nullptr, &size));
                std::vector<std::byte> storage(size);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                checked(info->GetMessage(i, message, &size));
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::cerr << message->pDescription << '\n';
                    throw std::runtime_error("debug_layer_error");
                }
            }
        std::cout
            << "PASS: vertex-to-stream-output 2816-byte bit-exact read; mixed integer/NaN bits; "
               "original 4096 bytes unchanged; predicated-off and incomplete output rejected; filled-byte "
               "counter verified; covering fence, same-recording repeat, unsubmitted retirement controls; adapter="
            << (hardware ? "hardware" : "WARP") << "; debug=" << (validation ? "ENABLED" : "UNAVAILABLE")
            << "; game=NOT_RUN; app_state_restore=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
