// SPDX-License-Identifier: MIT
#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "odv/core/core.hpp"
#include "odv/stereo/scene.hpp"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <optional>
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
    Com<ID3D12RootSignature> texture_root_;
    Com<ID3D12PipelineState> texture_pipeline_;
    Com<ID3D12DescriptorHeap> texture_srv_;
    DXGI_FORMAT texture_format_{};
    Com<ID3D12DescriptorHeap> rtv_, dsv_;
    Com<ID3D12Resource> upload_, depth_;
    Com<ID3D12QueryHeap> timestamp_heap_;
    Com<ID3D12Resource> timestamp_readback_;
    UINT64 timestamp_frequency_{};
    std::optional<double> last_gpu_ms_;
    UINT width_{}, height_{};
    UINT64 serial_{};
    HANDLE event_{};

  public:
    ~Renderer();
    void initialize(LUID adapter, D3D_FEATURE_LEVEL level, bool debug);
    void pipeline(DXGI_FORMAT format);
    void render(ID3D12Resource *target, DXGI_FORMAT format, UINT width, UINT height,
                const std::vector<stereo::Vertex> &vertices);
    // Stream images arrive in COMMON and are restored to COMMON. The target
    // remains RENDER_TARGET, as required by the OpenXR D3D12 graphics binding.
    void render_texture(ID3D12Resource *target, DXGI_FORMAT format, UINT width, UINT height, ID3D12Resource *source,
                        bool source_srgb = false);
    void sync();
    ID3D12Device *device() const { return device_.Get(); }
    ID3D12CommandQueue *queue() const { return queue_.Get(); }
    std::optional<double> last_gpu_ms() const { return last_gpu_ms_; }
};
} // namespace odv::d3d
