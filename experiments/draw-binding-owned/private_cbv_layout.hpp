// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <cstdint>
#include <d3d12.h>

namespace odv::research {
// Prepare only, before recording. The caller supplies an exact deserialized
// v1.1 layout, serializes/creates a NEW root and PSO, and rebinds every root
// argument. No application root, descriptor heap or shader is modified.
// Input and static samplers must outlive serialization. This object cannot
// move because the description points into its fixed storage.
// Only finite tables and a separately proven non-array, bounded CBV read are
// supported. This is not evidence that a game's root layout is compatible.
class PrivateCbvLayout final {
    std::array<D3D12_ROOT_PARAMETER1, 64> parameters_{};
    std::array<D3D12_DESCRIPTOR_RANGE1, 257> ranges_{};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC description_{};
    UINT private_index_ = UINT_MAX, cost_ = 0;

  public:
    PrivateCbvLayout() = default;
    PrivateCbvLayout(const PrivateCbvLayout &) = delete;
    PrivateCbvLayout &operator=(const PrivateCbvLayout &) = delete;
    bool prepare(const D3D12_ROOT_SIGNATURE_DESC1 &source, UINT table_index, UINT reg, UINT space,
                 bool non_array_bounded_cbv_proven) noexcept {
        description_ = {};
        private_index_ = UINT_MAX;
        cost_ = 0;
        if (!non_array_bounded_cbv_proven || !source.pParameters || source.NumParameters == 0 ||
            source.NumParameters > 64 || table_index >= source.NumParameters ||
            (source.NumStaticSamplers && !source.pStaticSamplers) ||
            (source.Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE))
            return false;
        const auto &target = source.pParameters[table_index];
        if (target.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
            (target.ShaderVisibility != D3D12_SHADER_VISIBILITY_VERTEX &&
             target.ShaderVisibility != D3D12_SHADER_VISIBILITY_ALL) ||
            (source.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS))
            return false;
        UINT count = 0, match_count = 0, target_count = 0;
        D3D12_ROOT_DESCRIPTOR_FLAGS data_flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
        for (UINT i = 0; i < source.NumParameters; ++i) {
            parameters_[i] = source.pParameters[i];
            auto &parameter = parameters_[i];
            switch (parameter.ParameterType) {
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                if (parameter.Constants.Num32BitValues > 64)
                    return false;
                cost_ += parameter.Constants.Num32BitValues;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                cost_ += 2;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
                const auto &table = source.pParameters[i].DescriptorTable;
                if (!table.pDescriptorRanges || !table.NumDescriptorRanges || table.NumDescriptorRanges > 256)
                    return false;
                const UINT start = count;
                std::uint64_t next_offset = 0;
                for (UINT j = 0; j < table.NumDescriptorRanges; ++j) {
                    auto range = table.pDescriptorRanges[j];
                    // Unbounded tables need a separate descriptor indexing proof.
                    if (!range.NumDescriptors || range.NumDescriptors == UINT_MAX ||
                        range.RangeType > D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                        return false;
                    const std::uint64_t offset =
                        range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                            ? next_offset
                            : range.OffsetInDescriptorsFromTableStart;
                    const std::uint64_t end = offset + range.NumDescriptors;
                    const std::uint64_t register_end = std::uint64_t(range.BaseShaderRegister) + range.NumDescriptors;
                    if (end > UINT_MAX || register_end > UINT_MAX)
                        return false;
                    next_offset = end;
                    range.OffsetInDescriptorsFromTableStart = static_cast<UINT>(offset);
                    const bool match = i == table_index && range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV &&
                                       range.RegisterSpace == space && reg >= range.BaseShaderRegister &&
                                       reg < register_end;
                    const auto add = [&](D3D12_DESCRIPTOR_RANGE1 value) noexcept {
                        if (!value.NumDescriptors)
                            return true;
                        if (count >= ranges_.size())
                            return false;
                        ranges_[count++] = value;
                        return true;
                    };
                    if (match) {
                        if (range.Flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS)
                            return false;
                        ++match_count;
                        data_flags = static_cast<D3D12_ROOT_DESCRIPTOR_FLAGS>(
                            range.Flags & (D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
                                           D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
                                           D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC));
                        const UINT left = reg - range.BaseShaderRegister;
                        auto before = range;
                        before.NumDescriptors = left;
                        auto after = range;
                        after.NumDescriptors -= left + 1;
                        after.BaseShaderRegister = reg + 1;
                        after.OffsetInDescriptorsFromTableStart += left + 1;
                        if (!add(before) || !add(after))
                            return false;
                    } else if (!add(range)) {
                        return false;
                    }
                }
                parameter.DescriptorTable = {count - start, ranges_.data() + start};
                if (i == table_index)
                    target_count = count - start;
                ++cost_;
                break;
            }
            default:
                return false;
            }
            if (cost_ > 64)
                return false;
        }
        if (match_count != 1)
            return false;
        UINT parameter_count = source.NumParameters;
        if (!target_count) {
            private_index_ = table_index;
            ++cost_; // One table DWORD becomes a two-DWORD root CBV.
        } else {
            if (parameter_count == 64)
                return false;
            private_index_ = parameter_count++;
            cost_ += 2;
        }
        if (cost_ > 64) {
            private_index_ = UINT_MAX;
            return false;
        }
        auto &lifted = parameters_[private_index_];
        lifted = {};
        lifted.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        lifted.ShaderVisibility = target.ShaderVisibility;
        lifted.Descriptor = {reg, space, data_flags};
        description_.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        description_.Desc_1_1 = {parameter_count, parameters_.data(), source.NumStaticSamplers, source.pStaticSamplers,
                                 source.Flags};
        return true;
    }
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *description() const noexcept {
        return description_.Version == D3D_ROOT_SIGNATURE_VERSION_1_1 ? &description_ : nullptr;
    }
    UINT private_index() const noexcept { return description() ? private_index_ : UINT_MAX; }
    UINT dword_cost() const noexcept { return description() ? cost_ : 0; }
};
} // namespace odv::research
