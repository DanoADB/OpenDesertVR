// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "deferred_resource_release.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3dcompiler.h>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace odv::research {
// Finite research reader for the 2816-byte camera candidate. Caller supplies
// actual vertex-stage usage and a prepared, allocation-free state replay.
// Not a generic application-state tracker and not enabled in a game adapter.
class VertexConstantStream final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    enum class Phase { empty, recorded, submitted, complete, retired };
    Phase phase_ = Phase::empty;
    Com<ID3D12Resource> source_, stream_, counter_, zero_, readback_;
    Com<ID3D12RootSignature> root_;
    Com<ID3D12PipelineState> pipeline_;
    Com<ID3D12QueryHeap> query_;
    Com<ID3D12Fence> fence_;
    UINT64 fence_value_ = 0;
    bool counted_ = false, executed_ = false;
    static inline std::atomic_uint32_t pending_ = 0;

    static void checked(HRESULT result) {
        if (FAILED(result))
            throw std::runtime_error("vertex_constant_stream_graphics_failure");
    }
    static void barrier(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after) noexcept {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        commands->ResourceBarrier(1, &b);
    }
    void prepare(ID3D12Device *device) {
        const auto buffer = [&](Com<ID3D12Resource> &resource, UINT64 bytes, D3D12_HEAP_TYPE type,
                                D3D12_RESOURCE_STATES state) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = type;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = bytes;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                    IID_PPV_ARGS(&resource)));
        };
        buffer(stream_, byte_count, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        buffer(counter_, 8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        buffer(zero_, 8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        buffer(readback_, counter_offset + 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        void *mapped = nullptr;
        const D3D12_RANGE empty{};
        checked(zero_->Map(0, &empty, &mapped));
        std::memset(mapped, 0, 8);
        zero_->Unmap(0, nullptr);
        constexpr char shader[] = R"(
cbuffer Input : register(b0) { uint4 words[176]; };
struct Output { float4 position : SV_Position; uint4 data : DATA; };
Output main(uint id : SV_VertexID) {
    Output o; o.position = float4(0, 0, 0, 1); o.data = words[id]; return o;
})";
        Com<ID3DBlob> bytecode, errors, serialized;
        checked(D3DCompile(shader, sizeof(shader) - 1, "owned-vertex-stream", nullptr, nullptr, "main", "vs_5_0",
                           D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors));
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 1;
        desc.pParameters = &parameter;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
        checked(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
        checked(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                            IID_PPV_ARGS(&root_)));
        const D3D12_SO_DECLARATION_ENTRY declaration{0, "DATA", 0, 0, 4, 0};
        const UINT stride = 16;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
        p.pRootSignature = root_.Get();
        p.VS = {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
        p.StreamOutput = {&declaration, 1, &stride, 1, D3D12_SO_NO_RASTERIZED_STREAM};
        p.SampleMask = UINT_MAX;
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE;
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        p.SampleDesc.Count = 1;
        checked(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline_)));
        D3D12_QUERY_HEAP_DESC q{D3D12_QUERY_HEAP_TYPE_SO_STATISTICS, 1, 0};
        checked(device->CreateQueryHeap(&q, IID_PPV_ARGS(&query_)));
    }
    void release_count() noexcept {
        if (counted_) {
            --pending_;
            counted_ = false;
        }
    }
    bool defer_all() noexcept {
        return DeferredResourceRelease::defer(source_, stream_, counter_, zero_, readback_, root_, pipeline_, query_,
                                              fence_);
    }

  public:
    static constexpr UINT byte_count = 2816, vertex_count = byte_count / 16;
    static constexpr UINT counter_offset = byte_count + sizeof(D3D12_QUERY_DATA_SO_STATISTICS);
    static constexpr UINT64 maximum_source_bytes = 64ULL * 1024 * 1024;
    static constexpr uint32_t maximum_pending = 6;
    struct Context {
        // These must come from the same command recording, not user settings.
        bool active_vertex_cbv = false;
        bool outside_render_pass = false;
        bool no_active_app_query = false;
        bool complete_replay_prepared = false;
        bool stream_output_bindings_known = false;
        bool unprotected = false;
    };
    struct Sample {
        const char *state;
        std::vector<std::byte> bytes;
    };

  private:
    std::optional<Sample> sample_;

  public:
    VertexConstantStream() = default;
    VertexConstantStream(const VertexConstantStream &) = delete;
    VertexConstantStream &operator=(const VertexConstantStream &) = delete;
    ~VertexConstantStream() {
        if (!counted_)
            return;
        // Unknown retirement/completion: quarantine resources AND capacity.
        // A completed recording may still execute again until reset/destroyed.
        source_.Detach();
        stream_.Detach();
        counter_.Detach();
        zero_.Detach();
        readback_.Detach();
        root_.Detach();
        pipeline_.Detach();
        query_.Detach();
        fence_.Detach();
    }
    template <class Restore>
    bool record(ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *source, UINT64 offset,
                const Context &context, Restore &&restore) {
        static_assert(std::is_nothrow_invocable_v<Restore>, "Replay must be prepared and noexcept");
        if (phase_ != Phase::empty || !device || !commands || !source ||
            commands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT || !context.active_vertex_cbv ||
            !context.outside_render_pass || !context.no_active_app_query || !context.complete_replay_prepared ||
            !context.stream_output_bindings_known || !context.unprotected || offset % 256)
            return false;
        const auto desc = source->GetDesc();
        D3D12_HEAP_PROPERTIES heap{};
        D3D12_HEAP_FLAGS flags{};
        if (FAILED(source->GetHeapProperties(&heap, &flags)) || heap.Type != D3D12_HEAP_TYPE_DEFAULT ||
            (flags & D3D12_HEAP_FLAG_HARDWARE_PROTECTED) || desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
            desc.Width > maximum_source_bytes || offset > desc.Width || byte_count > desc.Width - offset)
            return false;
        Com<ID3D12Device> source_device, list_device;
        if (FAILED(source->GetDevice(IID_PPV_ARGS(&source_device))) || source_device.Get() != device ||
            FAILED(commands->GetDevice(IID_PPV_ARGS(&list_device))) || list_device.Get() != device ||
            !source->GetGPUVirtualAddress() || source->GetGPUVirtualAddress() % 256)
            return false;
        if (pending_.fetch_add(1) >= maximum_pending) {
            --pending_;
            return false;
        }
        counted_ = true;
        try {
            source_ = source;
            prepare(device); // All allocation/compilation before any GPU command.
        } catch (...) {
            source_.Reset();
            stream_.Reset();
            counter_.Reset();
            zero_.Reset();
            readback_.Reset();
            root_.Reset();
            pipeline_.Reset();
            query_.Reset();
            release_count();
            throw;
        }
        barrier(commands, counter_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyBufferRegion(counter_.Get(), 0, zero_.Get(), 0, 8);
        barrier(commands, counter_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);
        barrier(commands, stream_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_STREAM_OUT);
        commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        commands->SetGraphicsRootSignature(root_.Get());
        commands->SetPipelineState(pipeline_.Get());
        commands->SetGraphicsRootConstantBufferView(0, source->GetGPUVirtualAddress() + offset);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        const D3D12_STREAM_OUTPUT_BUFFER_VIEW view{stream_->GetGPUVirtualAddress(), byte_count,
                                                   counter_->GetGPUVirtualAddress()};
        commands->SOSetTargets(0, 1, &view);
        commands->BeginQuery(query_.Get(), D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);
        commands->DrawInstanced(vertex_count, 1, 0, 0);
        commands->EndQuery(query_.Get(), D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);
        const D3D12_STREAM_OUTPUT_BUFFER_VIEW unbound{};
        commands->SOSetTargets(0, 1, &unbound);
        barrier(commands, stream_.Get(), D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(commands, counter_.Get(), D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands->CopyBufferRegion(readback_.Get(), 0, stream_.Get(), 0, byte_count);
        commands->CopyBufferRegion(readback_.Get(), counter_offset, counter_.Get(), 0, 8);
        commands->ResolveQueryData(query_.Get(), D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0, 1, readback_.Get(),
                                   byte_count);
        barrier(commands, stream_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        barrier(commands, counter_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        restore();
        phase_ = Phase::recorded;
        return true;
    }
    bool before_execute() noexcept {
        if (phase_ == Phase::empty || phase_ == Phase::retired)
            return false;
        executed_ = true;
        phase_ = Phase::recorded;
        return true;
    }
    bool submitted_after_execute(ID3D12Fence *fence, UINT64 value) noexcept {
        if (phase_ == Phase::empty || phase_ == Phase::retired || !fence || !value)
            return false;
        executed_ = true;
        fence_ = fence;
        fence_value_ = value;
        phase_ = Phase::submitted;
        return true;
    }
    Sample read() {
        if (sample_)
            return *sample_; // Immutable first sample, even on re-execution.
        if (phase_ != Phase::submitted)
            return {executed_ ? "GPU_PENDING" : "UNSUBMITTED", {}};
        const auto completed = fence_->GetCompletedValue();
        if (completed == UINT64_MAX)
            throw std::runtime_error("vertex_constant_stream_device_removed");
        if (completed < fence_value_)
            return {"GPU_PENDING", {}};
        void *mapped = nullptr;
        const D3D12_RANGE range{0, counter_offset + 8};
        checked(readback_->Map(0, &range, &mapped));
        try {
            D3D12_QUERY_DATA_SO_STATISTICS statistics{};
            UINT64 filled = 0;
            std::memcpy(&statistics, static_cast<const std::byte *>(mapped) + byte_count, sizeof(statistics));
            std::memcpy(&filled, static_cast<const std::byte *>(mapped) + counter_offset, sizeof(filled));
            Sample value{"SO_INCOMPLETE", {}};
            if (statistics.NumPrimitivesWritten == vertex_count && statistics.PrimitivesStorageNeeded == vertex_count &&
                filled == byte_count) {
                value.state = "READY";
                value.bytes.resize(byte_count);
                std::memcpy(value.bytes.data(), mapped, byte_count);
            } else if (!statistics.NumPrimitivesWritten && !statistics.PrimitivesStorageNeeded) {
                value.state = "SO_NOT_EXECUTED";
            }
            sample_ = std::move(value);
        } catch (...) {
            const D3D12_RANGE empty{};
            readback_->Unmap(0, &empty);
            throw;
        }
        const D3D12_RANGE empty{};
        readback_->Unmap(0, &empty);
        phase_ = Phase::complete;
        return *sample_;
    }
    // Caller proves successful recording reset/destruction and reports EVERY
    // execution, with the latest fence covering all of them (same queue).
    bool release_retired() noexcept {
        if ((phase_ != Phase::submitted && phase_ != Phase::complete) || !fence_)
            return false;
        const auto completed = fence_->GetCompletedValue();
        if (completed == UINT64_MAX || completed < fence_value_ || !defer_all())
            return false;
        phase_ = Phase::retired;
        release_count();
        return true;
    }
    bool discard_unsubmitted() noexcept {
        if (phase_ != Phase::recorded || executed_ || !defer_all())
            return false;
        phase_ = Phase::retired;
        release_count();
        return true;
    }
};
} // namespace odv::research
