// SPDX-License-Identifier: MIT
#pragma once
#include "native_indirect_events.hpp"

namespace odv::research {
inline bool clip_probe_signature(const NativeIndirectDescription &d) noexcept {
    return d.size == sizeof(d) && d.version == 1 && d.known == 1 && d.byte_stride == 40 && d.argument_count == 3 &&
           d.root_signature && d.root_mutation_mask == 1 && !d.vertex_buffer_mask && d.index_buffer_modified == 1 &&
           d.action == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED &&
           d.arguments[0].Type == D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW &&
           d.arguments[1].Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT &&
           d.arguments[1].Constant.RootParameterIndex == 0 && d.arguments[1].Constant.DestOffsetIn32BitValues == 0 &&
           d.arguments[1].Constant.Num32BitValuesToSet == 1 &&
           d.arguments[2].Type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
}
// Record the exact original indirect command once, with only the PSO changed.
// Original root, descriptor heaps/tables, CBVs, argument/count buffers and GPU
// values remain untouched. Native ExecuteIndirect performs its usual resets.
// The caller proves draw guards, matching current root and PSO ownership.
inline bool record_clip_indirect(ID3D12GraphicsCommandList *commands, ID3D12PipelineState *original,
                                 ID3D12PipelineState *variant, const NativeIndirectCall &call,
                                 const NativeIndirectDescription &description) noexcept {
    if (!commands || !original || !variant || !call.signature || !call.argument_buffer ||
        !clip_probe_signature(description))
        return false;
    commands->SetPipelineState(variant);
    commands->ExecuteIndirect(call.signature, call.maximum_count, call.argument_buffer, call.argument_offset,
                              call.count_buffer, call.count_offset);
    commands->SetPipelineState(original);
    return true;
}
} // namespace odv::research
