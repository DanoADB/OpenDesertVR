// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <d3d12.h>

namespace odv::research {
// Prepared BEFORE recording a reader draw. Native replay cannot allocate,
// resolve resources, inspect descriptor heaps or invoke framework callbacks.
// The frontend must prove all root arguments and all four SO slots are known.
struct PreparedGraphicsReplay final {
    enum class RootKind { unknown, table, cbv, srv, uav, constants };
    struct RootArgument {
        RootKind kind = RootKind::unknown;
        UINT64 address = 0;
        UINT count = 0;
        std::array<UINT, 64> values{};
    };
    ID3D12RootSignature *root = nullptr;
    ID3D12PipelineState *pipeline = nullptr;
    UINT root_count = 0, target_count = 0;
    std::array<RootArgument, 64> arguments;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> targets{};
    D3D12_CPU_DESCRIPTOR_HANDLE depth{};
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    std::array<D3D12_STREAM_OUTPUT_BUFFER_VIEW, 4> stream_outputs{};
    bool targets_known = false, stream_outputs_known = false;

    bool ready() const noexcept {
        if (!root || !pipeline || root_count > arguments.size() || target_count > targets.size() || !targets_known ||
            !stream_outputs_known || topology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
            return false;
        for (UINT i = 0; i < root_count; ++i) {
            const auto &argument = arguments[i];
            if (argument.kind == RootKind::unknown || (argument.kind == RootKind::table && !argument.address) ||
                (argument.kind == RootKind::constants && (!argument.count || argument.count > argument.values.size())))
                return false;
        }
        return true;
    }
    // Call only after ready() succeeds. Raw pointers are borrowed during the
    // same valid application callback; its own draw retains original objects.
    void apply(ID3D12GraphicsCommandList *commands) const noexcept {
        commands->SetGraphicsRootSignature(root);
        commands->SetPipelineState(pipeline);
        for (UINT i = 0; i < root_count; ++i) {
            const auto &argument = arguments[i];
            switch (argument.kind) {
            case RootKind::table:
                commands->SetGraphicsRootDescriptorTable(i, {argument.address});
                break;
            case RootKind::cbv:
                commands->SetGraphicsRootConstantBufferView(i, argument.address);
                break;
            case RootKind::srv:
                commands->SetGraphicsRootShaderResourceView(i, argument.address);
                break;
            case RootKind::uav:
                commands->SetGraphicsRootUnorderedAccessView(i, argument.address);
                break;
            case RootKind::constants:
                commands->SetGraphicsRoot32BitConstants(i, argument.count, argument.values.data(), 0);
                break;
            default:
                break;
            }
        }
        commands->OMSetRenderTargets(target_count, targets.data(), FALSE, depth.ptr ? &depth : nullptr);
        commands->IASetPrimitiveTopology(topology);
        commands->SOSetTargets(0, static_cast<UINT>(stream_outputs.size()), stream_outputs.data());
    }
};
} // namespace odv::research
