// SPDX-License-Identifier: MIT
#pragma once
#include "../renodx-owned/native_indirect_events.hpp"
#include <utility>
#include <wrl/client.h>

namespace odv::research {
// Preparation only. Never reads or edits GPU argument/count buffers and never
// records a draw. Caller must bind the derived root/PSO, forward the ORIGINAL
// native call exactly once, preserve post-indirect resets, and retain objects
// through all executions. Game interception and GPU replay remain separate.
// The derived root must preserve all original indirect root indices, types
// and register mappings; native compatibility alone cannot prove that contract.
class PrivateIndirectSignature final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    Com<ID3D12CommandSignature> original_, replacement_;
    Com<ID3D12RootSignature> original_root_, private_root_;

  public:
    bool prepare(ID3D12Device *device, ID3D12CommandSignature *original, ID3D12RootSignature *original_root,
                 ID3D12RootSignature *private_root, UINT private_cbv_index) noexcept {
        if (replacement_ || !device || !original || !original_root || !private_root || original_root == private_root ||
            private_cbv_index >= 64)
            return false;
        NativeIndirectDescription metadata;
        if (!load_indirect_description(original, metadata) ||
            metadata.root_signature != reinterpret_cast<std::uintptr_t>(original_root) ||
            (metadata.root_mutation_mask & (UINT64_C(1) << private_cbv_index)))
            return false;
        // The measured game path changes IBV + one root word + DrawIndexed.
        // Reject other formats until their reset/rebinding behavior is tested.
        if (metadata.argument_count != 3 || metadata.byte_stride != 40 ||
            metadata.arguments[0].Type != D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW ||
            metadata.arguments[1].Type != D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT ||
            metadata.arguments[1].Constant.RootParameterIndex != 0 ||
            metadata.arguments[1].Constant.DestOffsetIn32BitValues != 0 ||
            metadata.arguments[1].Constant.Num32BitValuesToSet != 1 ||
            metadata.arguments[2].Type != D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED)
            return false;
        const D3D12_COMMAND_SIGNATURE_DESC desc{metadata.byte_stride, metadata.argument_count,
                                                metadata.arguments.data(), metadata.node_mask};
        Com<ID3D12CommandSignature> result;
        if (FAILED(device->CreateCommandSignature(&desc, private_root, IID_PPV_ARGS(&result))))
            return false;
        original_ = original;
        original_root_ = original_root;
        private_root_ = private_root;
        replacement_ = std::move(result);
        return true;
    }
    ID3D12CommandSignature *replacement() const noexcept { return replacement_.Get(); }
};
} // namespace odv::research
