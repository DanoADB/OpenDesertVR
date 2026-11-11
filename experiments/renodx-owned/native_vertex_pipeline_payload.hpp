// SPDX-License-Identifier: MIT
#pragma once
#include "native_vertex_pipeline_source.hpp"
#include <atomic>
#include <mutex>
#include <new>

namespace odv::research {
// Process-local COM contract; no shader bytes are serialized or exported.
struct __declspec(uuid("7711D98A-9717-4C74-BB8A-292D45F49751")) INativeVertexPipelineSource : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetVertex(D3D12_SHADER_BYTECODE *vertex) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVariant(UINT slot, ID3D12PipelineState **pipeline) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVariant(ID3D12Device *device, UINT slot, const void *vertex, SIZE_T bytes,
                                                    ID3D12PipelineState **pipeline) noexcept = 0;
};

class NativeVertexPipelinePayload final : public INativeVertexPipelineSource {
    std::atomic<ULONG> references_{1};
    NativeVertexPipelineSource source_;
    std::mutex mutex_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 3> variants_;

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) noexcept override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(INativeVertexPipelineSource))
            return E_NOINTERFACE;
        *out = static_cast<INativeVertexPipelineSource *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const auto remaining = --references_;
        if (!remaining)
            delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetVertex(D3D12_SHADER_BYTECODE *vertex) noexcept override {
        if (!vertex)
            return E_POINTER;
        *vertex = source_.vertex();
        return vertex->BytecodeLength ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE GetVariant(UINT slot, ID3D12PipelineState **pipeline) noexcept override {
        if (!pipeline)
            return E_POINTER;
        *pipeline = nullptr;
        if (slot >= variants_.size())
            return E_INVALIDARG;
        try {
            const std::lock_guard lock(mutex_);
            return variants_[slot] ? variants_[slot].CopyTo(pipeline) : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        } catch (...) {
            return E_FAIL;
        }
    }
    HRESULT STDMETHODCALLTYPE CreateVariant(ID3D12Device *device, UINT slot, const void *vertex, SIZE_T bytes,
                                            ID3D12PipelineState **pipeline) noexcept override {
        if (!pipeline)
            return E_POINTER;
        *pipeline = nullptr;
        if (!device || slot >= variants_.size())
            return E_INVALIDARG;
        try {
            const std::lock_guard lock(mutex_);
            // Each slot is a one-shot immutable variant, not a shader cache.
            if (variants_[slot])
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            Microsoft::WRL::ComPtr<ID3D12PipelineState> result;
            const HRESULT hr = source_.create_variant(device, vertex, bytes, &result);
            if (FAILED(hr))
                return hr;
            variants_[slot] = result;
            *pipeline = result.Detach();
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }
    template <class Description>
    static HRESULT attach(ID3D12PipelineState *pipeline, const Description &description) noexcept {
        if (!pipeline)
            return E_POINTER;
        UINT existing_bytes = 0;
        if (SUCCEEDED(pipeline->GetPrivateData(__uuidof(INativeVertexPipelineSource), &existing_bytes, nullptr)))
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        try {
            Microsoft::WRL::ComPtr<NativeVertexPipelinePayload> payload;
            payload.Attach(new NativeVertexPipelinePayload);
            if (!payload->source_.prepare(description))
                return E_INVALIDARG;
            // The original PSO owns the source and bounded private variants.
            // It is restored in the same recording as the private draw, so
            // application PSO lifetime must already cover that GPU recording.
            // Payload has no reference to the original: there is no COM cycle.
            return pipeline->SetPrivateDataInterface(__uuidof(INativeVertexPipelineSource), payload.Get());
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }
};

inline Microsoft::WRL::ComPtr<INativeVertexPipelineSource> load_native_vertex_source(ID3D12PipelineState *pipeline) {
    Microsoft::WRL::ComPtr<INativeVertexPipelineSource> result;
    if (pipeline) {
        UINT bytes = sizeof(INativeVertexPipelineSource *);
        if (FAILED(pipeline->GetPrivateData(__uuidof(INativeVertexPipelineSource), &bytes, result.GetAddressOf())) ||
            bytes != sizeof(INativeVertexPipelineSource *))
            result.Reset();
    }
    return result;
}
} // namespace odv::research
