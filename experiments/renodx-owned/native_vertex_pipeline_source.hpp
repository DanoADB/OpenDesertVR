// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <memory>
#include <vector>
#include <wrl/client.h>

namespace odv::research {
// Process-private exact native source for a VS/PS-only probe. Every pointer
// consumed after creation is retained or deep-copied. No generic pipeline
// conversion, shader export, application descriptor or resource write.
class NativeVertexPipelineSource final {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC legacy_{};
    std::vector<std::byte> stream_;
    std::vector<std::unique_ptr<std::byte[]>> blocks_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    D3D12_SHADER_BYTECODE vertex_{};
    std::size_t vertex_offset_ = 0, bytes_ = 0;
    bool prepared_ = false, is_stream_ = false, attempted_ = false, had_cache_ = false;
    unsigned stages_ = 0;
    static std::size_t align(std::size_t value, std::size_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }
    template <class T> T *copy(const T *source, std::size_t count, std::size_t limit) {
        if (!count)
            return nullptr;
        if (!source || count > limit || count > 8 * 1024 * 1024 / sizeof(T) ||
            bytes_ + count * sizeof(T) > 8 * 1024 * 1024)
            return nullptr;
        auto storage = std::make_unique<std::byte[]>(count * sizeof(T));
        std::memcpy(storage.get(), source, count * sizeof(T));
        bytes_ += count * sizeof(T);
        auto *result = reinterpret_cast<T *>(storage.get());
        blocks_.push_back(std::move(storage));
        return result;
    }
    bool shader(D3D12_SHADER_BYTECODE &code, unsigned stage) {
        if (!code.BytecodeLength) {
            code = {};
            return true;
        }
        auto *data = copy(static_cast<const std::byte *>(code.pShaderBytecode), code.BytecodeLength, 1024 * 1024);
        if (!data)
            return false;
        code.pShaderBytecode = data;
        stages_ |= stage;
        if (stage == 1)
            vertex_ = code;
        return true;
    }
    const char *semantic(const char *source) {
        if (!source)
            return nullptr;
        std::size_t length = 0;
        while (length <= 256 && source[length])
            ++length;
        return length > 256 ? nullptr : copy(source, length + 1, 257);
    }
    bool input(D3D12_INPUT_LAYOUT_DESC &layout) {
        auto *items = copy(layout.pInputElementDescs, layout.NumElements, 64);
        if (layout.NumElements && !items)
            return false;
        for (UINT i = 0; i < layout.NumElements; ++i) {
            items[i].SemanticName = semantic(items[i].SemanticName);
            if (!items[i].SemanticName)
                return false;
        }
        layout.pInputElementDescs = items;
        return true;
    }
    bool output(D3D12_STREAM_OUTPUT_DESC &description) {
        if (description.NumEntries || description.NumStrides)
            return false;
        description.pSODeclaration = nullptr;
        description.pBufferStrides = nullptr;
        return true;
    }
    template <class T, class F> bool visit(std::size_t &position, F callback) {
        const auto data = position + align(sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE), alignof(T));
        const auto end = align(data + sizeof(T), alignof(void *));
        if (end > stream_.size())
            return false;
        T value;
        std::memcpy(&value, stream_.data() + data, sizeof(T));
        if (!callback(value, data))
            return false;
        std::memcpy(stream_.data() + data, &value, sizeof(T));
        position = end;
        return true;
    }
    template <class T> bool scalar(std::size_t &position) {
        return visit<T>(position, [](T &, std::size_t) { return true; });
    }

  public:
    NativeVertexPipelineSource() = default;
    NativeVertexPipelineSource(const NativeVertexPipelineSource &) = delete;
    NativeVertexPipelineSource &operator=(const NativeVertexPipelineSource &) = delete;
    bool prepare(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &source) {
        if (attempted_)
            return false;
        attempted_ = true;
        legacy_ = source;
        if (!source.pRootSignature)
            return false;
        root_ = source.pRootSignature;
        had_cache_ = source.CachedPSO.CachedBlobSizeInBytes != 0;
        legacy_.CachedPSO = {};
        if (!shader(legacy_.VS, 1) || !shader(legacy_.PS, 2) || !shader(legacy_.HS, 4) || !shader(legacy_.DS, 8) ||
            !shader(legacy_.GS, 16) || !input(legacy_.InputLayout) || !output(legacy_.StreamOutput))
            return false;
        prepared_ = stages_ == 3;
        return prepared_;
    }
    bool prepare(const D3D12_PIPELINE_STATE_STREAM_DESC &source) {
        if (attempted_)
            return false;
        attempted_ = true;
        is_stream_ = true;
        if (!source.pPipelineStateSubobjectStream || !source.SizeInBytes || source.SizeInBytes > 4096)
            return false;
        stream_.resize(source.SizeInBytes);
        std::memcpy(stream_.data(), source.pPipelineStateSubobjectStream, source.SizeInBytes);
        if (reinterpret_cast<std::uintptr_t>(stream_.data()) % alignof(void *))
            return false;
        std::uint64_t seen = 0;
        unsigned count = 0;
        for (std::size_t p = 0; p < stream_.size();) {
            if (++count > 64 || stream_.size() - p < sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE))
                return false;
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
            std::memcpy(&type, stream_.data() + p, sizeof(type));
            const auto id = static_cast<unsigned>(type);
            if (id >= 64 || (seen & (std::uint64_t(1) << id)))
                return false;
            seen |= std::uint64_t(1) << id;
            bool ok = false;
            switch (type) {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
                ok = visit<ID3D12RootSignature *>(p, [&](auto &v, std::size_t) {
                    if (!v)
                        return false;
                    root_ = v;
                    return true;
                });
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
                ok = visit<D3D12_SHADER_BYTECODE>(p, [&](auto &v, std::size_t offset) {
                    vertex_offset_ = offset;
                    return shader(v, 1);
                });
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
                ok = visit<D3D12_SHADER_BYTECODE>(p, [&](auto &v, std::size_t) { return shader(v, 2); });
                break;
            // Reject stages that could replace or reinterpret the modified VS
            // position. Unknown stream types also fail closed.
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
                ok = visit<D3D12_SHADER_BYTECODE>(p, [](auto &v, std::size_t) { return v.BytecodeLength == 0; });
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
                ok = visit<D3D12_STREAM_OUTPUT_DESC>(p, [&](auto &v, std::size_t) { return output(v); });
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
                ok = visit<D3D12_INPUT_LAYOUT_DESC>(p, [&](auto &v, std::size_t) { return input(v); });
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
                ok = visit<D3D12_VIEW_INSTANCING_DESC>(p, [&](auto &v, std::size_t) {
                    auto *locations =
                        copy(v.pViewInstanceLocations, v.ViewInstanceCount, D3D12_MAX_VIEW_INSTANCE_COUNT);
                    if (v.ViewInstanceCount && !locations)
                        return false;
                    v.pViewInstanceLocations = locations;
                    return true;
                });
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
                ok = visit<D3D12_CACHED_PIPELINE_STATE>(p, [&](auto &v, std::size_t) {
                    had_cache_ = v.CachedBlobSizeInBytes != 0;
                    v = {};
                    return true;
                });
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
                ok = scalar<D3D12_BLEND_DESC>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
                ok = scalar<UINT>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
                ok = scalar<D3D12_RASTERIZER_DESC>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
                ok = scalar<D3D12_RASTERIZER_DESC1>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
                ok = scalar<D3D12_RASTERIZER_DESC2>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
                ok = scalar<D3D12_DEPTH_STENCIL_DESC>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
                ok = scalar<D3D12_DEPTH_STENCIL_DESC1>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
                ok = scalar<D3D12_DEPTH_STENCIL_DESC2>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
                ok = scalar<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
                ok = scalar<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
                ok = scalar<D3D12_RT_FORMAT_ARRAY>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
                ok = scalar<DXGI_FORMAT>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
                ok = scalar<DXGI_SAMPLE_DESC>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
                ok = scalar<UINT>(p);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
                ok = scalar<D3D12_PIPELINE_STATE_FLAGS>(p);
                break;
            default:
                return false;
            }
            if (!ok)
                return false;
        }
        prepared_ = root_ && stages_ == 3;
        return prepared_;
    }
    HRESULT create_variant(ID3D12Device *device, const void *vertex, std::size_t size,
                           ID3D12PipelineState **out) const {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!prepared_ || !device || !vertex || !size || size > 1024 * 1024)
            return E_INVALIDARG;
        if (!is_stream_) {
            auto description = legacy_;
            description.VS = {vertex, size};
            return device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(out));
        }
        auto stream = stream_;
        const D3D12_SHADER_BYTECODE replacement{vertex, size};
        std::memcpy(stream.data() + vertex_offset_, &replacement, sizeof(replacement));
        const D3D12_PIPELINE_STATE_STREAM_DESC description{stream.size(), stream.data()};
        Microsoft::WRL::ComPtr<ID3D12Device2> device2;
        const HRESULT queried = device->QueryInterface(IID_PPV_ARGS(&device2));
        return FAILED(queried) ? queried : device2->CreatePipelineState(&description, IID_PPV_ARGS(out));
    }
    ID3D12RootSignature *root() const noexcept { return prepared_ ? root_.Get() : nullptr; }
    D3D12_SHADER_BYTECODE vertex() const noexcept { return prepared_ ? vertex_ : D3D12_SHADER_BYTECODE{}; }
    bool stream_source() const noexcept { return is_stream_; }
    bool cleared_cache() const noexcept { return had_cache_; }
};
} // namespace odv::research
