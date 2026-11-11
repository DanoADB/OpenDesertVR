// SPDX-License-Identifier: MIT
#include "../renodx-owned/native_indirect_events.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool value) {
    if (!value)
        throw std::runtime_error("signature_metadata_control_failed");
}
int main() {
    try {
        // Synthetic API descriptors/identities only. No native signature object
        // or post-indirect GPU state is tested by this executable.
        D3D12_INDIRECT_ARGUMENT_DESC draw{};
        draw.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC desc{sizeof(D3D12_DRAW_ARGUMENTS), 1, &draw, 0};
        odv::research::NativeIndirectDescription metadata;
        require(odv::research::describe_indirect_signature(&desc, nullptr, metadata));
        require(metadata.known && !metadata.root_mutation_mask && !metadata.root_signature &&
                !metadata.vertex_buffer_mask && !metadata.index_buffer_modified);
        auto *root = reinterpret_cast<ID3D12RootSignature *>(std::uintptr_t{123});
        std::array<D3D12_INDIRECT_ARGUMENT_DESC, 2> root_cbv{};
        root_cbv[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
        root_cbv[0].ConstantBufferView.RootParameterIndex = 3;
        root_cbv[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        desc = {8 + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), 2, root_cbv.data(), 0};
        require(odv::research::describe_indirect_signature(&desc, root, metadata));
        require(metadata.root_mutation_mask == 8 && metadata.root_signature == 123 &&
                metadata.action == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED);
        require(!odv::research::describe_indirect_signature(&desc, nullptr, metadata));
        root_cbv[0].ConstantBufferView.RootParameterIndex = 64;
        require(!odv::research::describe_indirect_signature(&desc, root, metadata));
        std::array<D3D12_INDIRECT_ARGUMENT_DESC, 3> mixed{};
        mixed[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        mixed[0].Constant = {1, 2, 2};
        mixed[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
        mixed[1].VertexBuffer.Slot = 4;
        mixed[2] = draw;
        desc = {8 + sizeof(D3D12_VERTEX_BUFFER_VIEW) + sizeof(D3D12_DRAW_ARGUMENTS), 3, mixed.data(), 0};
        require(odv::research::describe_indirect_signature(&desc, root, metadata));
        require(metadata.root_mutation_mask == 2 && metadata.vertex_buffer_mask == 16 &&
                metadata.arguments[0].Constant.DestOffsetIn32BitValues == 2);
        --desc.ByteStride;
        require(!odv::research::describe_indirect_signature(&desc, root, metadata));
        mixed[0].Type = static_cast<D3D12_INDIRECT_ARGUMENT_TYPE>(255);
        desc.ByteStride = 128;
        require(!odv::research::describe_indirect_signature(&desc, root, metadata));
        desc.NumArgumentDescs = 65;
        require(!odv::research::describe_indirect_signature(&desc, root, metadata));
        require(!odv::research::load_indirect_description(nullptr, metadata));
        std::cout << "PASS: synthetic draw-only/root-CBV/constants+VB/unknown signature metadata; "
                     "native_signature=NOT_RUN; post_indirect_gpu_state=NOT_RUN; game=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
