// SPDX-License-Identifier: MIT
#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "odv/core/core.hpp"
#include "odv/stereo/scene.hpp"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

namespace odv::d3d {
template <class T> using Com = Microsoft::WRL::ComPtr<T>;
void hr(HRESULT result, const char *operation);
class Renderer {
    Com<ID3D12Device> device_;
    Com<ID3D12CommandQueue> queue_;
    Com<ID3D12CommandAllocator> allocator_;
    Com<ID3D12GraphicsCommandList> commands_;
    Com<ID3D12Fence> fence_;
    Com<ID3D12RootSignature> root_;
    Com<ID3D12PipelineState> pipeline_;
    Com<ID3D12DescriptorHeap> rtv_, dsv_;
    Com<ID3D12Resource> upload_, depth_;
    UINT width_{}, height_{};
    UINT64 serial_{};
    HANDLE event_{};

  public:
    ~Renderer();
    void initialize(LUID adapter, D3D_FEATURE_LEVEL level, bool debug);
    void pipeline(DXGI_FORMAT format);
    void render(ID3D12Resource *target, DXGI_FORMAT format, UINT width, UINT height,
                const std::vector<stereo::Vertex> &vertices);
    void sync();
    ID3D12Device *device() const { return device_.Get(); }
    ID3D12CommandQueue *queue() const { return queue_.Get(); }
};
} // namespace odv::d3d
