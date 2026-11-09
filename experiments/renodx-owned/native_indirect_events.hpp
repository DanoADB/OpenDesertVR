// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <type_traits>

namespace odv::research {
inline constexpr GUID native_signature_metadata_guid = {
    0x9ae41b75, 0x36c9, 0x4b4b, {0xa9, 0x71, 0x70, 0x13, 0x26, 0x8e, 0x02, 0x94}};
inline constexpr std::uint8_t native_indirect_events_guid[16] = {0x32, 0x6a, 0x83, 0x55, 0xc1, 0x24, 0x45, 0x2d,
                                                                 0x82, 0xed, 0x91, 0x0f, 0x56, 0x1c, 0xe9, 0x33};
struct NativeIndirectDescription final {
    std::uint32_t size, version, known, byte_stride, argument_count, node_mask;
    std::uintptr_t root_signature;
    std::uint64_t root_mutation_mask;
    std::uint32_t vertex_buffer_mask, index_buffer_modified, action;
    std::array<D3D12_INDIRECT_ARGUMENT_DESC, 64> arguments;
};
static_assert(std::is_trivially_copyable_v<NativeIndirectDescription>);

// Copy ONLY documented active fields. Zero padding/unused union bytes rather
// than copying opaque application stack bytes into the private metadata tag.
inline bool describe_indirect_signature(const D3D12_COMMAND_SIGNATURE_DESC *desc, ID3D12RootSignature *root,
                                        NativeIndirectDescription &result) noexcept {
    std::memset(&result, 0, sizeof(result));
    result.size = sizeof(result);
    result.version = 1;
    if (!desc || !desc->NumArgumentDescs || desc->NumArgumentDescs > result.arguments.size() || !desc->pArgumentDescs ||
        !desc->ByteStride || desc->ByteStride % 4)
        return false;
    result.byte_stride = desc->ByteStride;
    result.argument_count = desc->NumArgumentDescs;
    result.node_mask = desc->NodeMask;
    result.root_signature = reinterpret_cast<std::uintptr_t>(root);
    std::uint64_t minimum_bytes = 0;
    bool terminal = false;
    for (UINT i = 0; i < desc->NumArgumentDescs; ++i) {
        const auto &source = desc->pArgumentDescs[i];
        auto &target = result.arguments[i];
        target.Type = source.Type;
        UINT root_index = 64;
        switch (source.Type) {
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
            if (i + 1 != desc->NumArgumentDescs || terminal)
                return false;
            terminal = true;
            result.action = static_cast<UINT>(source.Type);
            minimum_bytes += source.Type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW ? sizeof(D3D12_DRAW_ARGUMENTS)
                             : source.Type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
                                 ? sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
                                 : sizeof(D3D12_DISPATCH_ARGUMENTS);
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
            if (source.VertexBuffer.Slot >= 32)
                return false;
            target.VertexBuffer.Slot = source.VertexBuffer.Slot;
            result.vertex_buffer_mask |= UINT32_C(1) << source.VertexBuffer.Slot;
            minimum_bytes += sizeof(D3D12_VERTEX_BUFFER_VIEW);
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
            result.index_buffer_modified = 1;
            minimum_bytes += sizeof(D3D12_INDEX_BUFFER_VIEW);
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            if (source.Constant.RootParameterIndex >= 64 || !source.Constant.Num32BitValuesToSet ||
                source.Constant.DestOffsetIn32BitValues >= 64 ||
                source.Constant.Num32BitValuesToSet > 64 - source.Constant.DestOffsetIn32BitValues)
                return false;
            target.Constant = source.Constant;
            root_index = source.Constant.RootParameterIndex;
            minimum_bytes += UINT64_C(4) * source.Constant.Num32BitValuesToSet;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            root_index = source.ConstantBufferView.RootParameterIndex;
            target.ConstantBufferView.RootParameterIndex = root_index;
            minimum_bytes += 8;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            root_index = source.ShaderResourceView.RootParameterIndex;
            target.ShaderResourceView.RootParameterIndex = root_index;
            minimum_bytes += 8;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            root_index = source.UnorderedAccessView.RootParameterIndex;
            target.UnorderedAccessView.RootParameterIndex = root_index;
            minimum_bytes += 8;
            break;
        default:
            return false; // Unknown/new signature types remain unqualified.
        }
        if (source.Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT ||
            source.Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW ||
            source.Type == D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW ||
            source.Type == D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW) {
            if (!root || root_index >= 64)
                return false;
            result.root_mutation_mask |= UINT64_C(1) << root_index;
        }
    }
    if (!terminal || minimum_bytes > result.byte_stride)
        return false;
    result.known = 1;
    return true;
}
inline bool load_indirect_description(ID3D12CommandSignature *signature, NativeIndirectDescription &result) noexcept {
    if (!signature)
        return false;
    UINT size = sizeof(result);
    if (FAILED(signature->GetPrivateData(native_signature_metadata_guid, &size, &result)) || size != sizeof(result) ||
        result.size != sizeof(result) || result.version != 1 || result.known != 1)
        return false;
    D3D12_COMMAND_SIGNATURE_DESC desc{};
    desc.ByteStride = result.byte_stride;
    desc.NumArgumentDescs = result.argument_count;
    desc.pArgumentDescs = result.arguments.data();
    desc.NodeMask = result.node_mask;
    NativeIndirectDescription canonical;
    return describe_indirect_signature(&desc, reinterpret_cast<ID3D12RootSignature *>(result.root_signature),
                                       canonical) &&
           std::memcmp(&result, &canonical, sizeof(result)) == 0;
}
struct NativeIndirectCall final {
    ID3D12CommandSignature *signature;
    UINT maximum_count;
    ID3D12Resource *argument_buffer;
    UINT64 argument_offset;
    ID3D12Resource *count_buffer;
    UINT64 count_offset;
};
// These are CPU command-recording boundaries, NOT GPU execution/completion.
enum class NativeIndirectPhase : std::uint32_t { before_record = 1, after_record = 2, cancelled = 3 };
struct NativeIndirectEvents final {
    std::uint32_t size = sizeof(NativeIndirectEvents), version = 1;
    void *context = nullptr;
    void(__cdecl *record)(void *, const NativeIndirectCall *, const NativeIndirectDescription *,
                          NativeIndirectPhase) noexcept = nullptr;
    void(__cdecl *capabilities)(void *, std::uint32_t) noexcept = nullptr;
};
static_assert(sizeof(void *) == 8 && sizeof(NativeIndirectEvents) == 32);
template <class CommandList> NativeIndirectEvents *get_native_indirect_events(CommandList *list) noexcept {
    std::uint64_t value = 0;
    list->get_private_data(native_indirect_events_guid, &value);
    auto *events = reinterpret_cast<NativeIndirectEvents *>(value);
    return events && events->size == sizeof(*events) && events->version == 1 ? events : nullptr;
}
} // namespace odv::research
