// SPDX-License-Identifier: MIT
#include "odv/d3d12/renderer.hpp"
#include <cmath>
#include <cstring>
#include <d3d12sdklayers.h>
#include <iostream>
#include <stdexcept>
using namespace odv;
using namespace odv::d3d;
namespace {
constexpr UINT width = 512, height = 512;
std::array<double, 3> capture(Renderer &graphics, const math::Pose &pose) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Com<ID3D12Resource> texture;
    hr(graphics.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture)),
       "test target");
    const auto vertices = stereo::vertices(pose, {-0.7, 0.7, 0.7, -0.7}, 0.05, 100);
    graphics.render(texture.Get(), desc.Format, width, height, vertices);
    if (!graphics.last_gpu_ms() || !std::isfinite(*graphics.last_gpu_ms()) || *graphics.last_gpu_ms() < 0)
        throw std::runtime_error("WARP GPU timestamps were not measured");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 row_size = 0, total = 0;
    graphics.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_size, &total);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    Com<ID3D12Resource> readback;
    hr(graphics.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr, IID_PPV_ARGS(&readback)),
       "readback");
    Com<ID3D12CommandAllocator> allocator;
    Com<ID3D12GraphicsCommandList> list;
    hr(graphics.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
       "test allocator");
    hr(graphics.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&list)),
       "test list");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                          D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    hr(list->Close(), "close test list");
    ID3D12CommandList *lists[] = {list.Get()};
    graphics.queue()->ExecuteCommandLists(1, lists);
    graphics.sync();
    void *data{};
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    hr(readback->Map(0, &range, &data), "readback map");
    std::array<double, 3> sum{};
    std::array<unsigned, 3> count{};
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const auto *pixel =
                static_cast<unsigned char *>(data) + footprint.Offset + y * footprint.Footprint.RowPitch + x * 4;
            for (int channel = 0; channel < 3; ++channel)
                if (pixel[channel] > 80 && pixel[channel] > pixel[(channel + 1) % 3] * 1.6 &&
                    pixel[channel] > pixel[(channel + 2) % 3] * 1.6) {
                    sum[channel] += x;
                    ++count[channel];
                }
        }
    D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    for (int i = 0; i < 3; ++i) {
        if (count[i] < 100)
            throw std::runtime_error("calibration cube not rasterized");
        sum[i] /= count[i];
    }
    return sum;
}
} // namespace
int main(int argc, char **argv) {
    try {
        const bool debug = argc == 2 && std::string(argv[1]) == "--debug-layer";
        if (debug) {
            Com<ID3D12Debug> layer;
            if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)))) {
                std::cout << "D3D12 debug layer unavailable; validation NOT_RUN\n";
                return 77;
            }
            layer->EnableDebugLayer();
        }
        // Exercise actual HRESULT classification used for every native allocation.
        try {
            hr(E_OUTOFMEMORY, "injected allocation");
            return 1;
        } catch (const Failure &e) {
            if (e.code != ErrorCode::allocation)
                return 1;
        }
        try {
            hr(DXGI_ERROR_DEVICE_REMOVED, "injected device loss");
            return 1;
        } catch (const Failure &e) {
            if (e.code != ErrorCode::device)
                return 1;
        }
        Com<IDXGIFactory4> factory;
        hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "test factory");
        try {
            Renderer invalid_adapter;
            invalid_adapter.initialize({0xffffffffu, -1}, D3D_FEATURE_LEVEL_11_0, false);
            return 1;
        } catch (const Failure &e) {
            if (e.code != ErrorCode::adapter)
                return 1;
        }
        Com<IDXGIAdapter1> warp;
        hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
        DXGI_ADAPTER_DESC1 desc{};
        hr(warp->GetDesc1(&desc), "WARP description");
        Renderer graphics;
        graphics.initialize(desc.AdapterLuid, D3D_FEATURE_LEVEL_11_0, false);
        graphics.pipeline(DXGI_FORMAT_R8G8B8A8_UNORM);
        const auto left = capture(graphics, math::eye_pose({}, -0.032)),
                   right = capture(graphics, math::eye_pose({}, 0.032));
        const double near_delta = left[0] - right[0], mid_delta = left[1] - right[1], far_delta = left[2] - right[2];
        std::cout << "WARP raster disparity pixels near/mid/far: " << near_delta << '/' << mid_delta << '/' << far_delta
                  << '\n';
        if (!(near_delta > mid_delta && mid_delta > far_delta && far_delta > 0.5))
            throw std::runtime_error("rasterized eye disparity invalid");
        if (debug) {
            Com<ID3D12InfoQueue> messages;
            hr(graphics.device()->QueryInterface(IID_PPV_ARGS(&messages)), "D3D12 validation queue");
            for (UINT64 i = 0; i < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T size = 0;
                hr(messages->GetMessage(i, nullptr, &size), "validation message size");
                std::vector<unsigned char> bytes(size);
                auto message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
                hr(messages->GetMessage(i, message, &size), "validation message");
                if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    message->Severity == D3D12_MESSAGE_SEVERITY_ERROR)
                    throw std::runtime_error(message->pDescription);
            }
            std::cout << "D3D12 validation: no corruption or error messages\n";
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
