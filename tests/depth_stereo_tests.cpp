// SPDX-License-Identifier: MIT
#include "odv/d3d12/depth_preview.hpp"
#include <cmath>
#include <cstring>
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>
#include <limits>

using namespace odv;
using namespace odv::d3d;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const Failure &) {
        rejected = true;
    }
    require(rejected, "invalid RGBD input accepted");
}
std::vector<uint32_t> read(Renderer &g, ID3D12Resource *image, D3D12_RESOURCE_STATES state) {
    auto desc = image->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes{};
    g.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes;
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    Com<ID3D12Resource> output;
    hr(g.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                           nullptr, IID_PPV_ARGS(&output)),
       "test readback");
    Com<ID3D12CommandAllocator> allocator;
    Com<ID3D12GraphicsCommandList> list;
    hr(g.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "test allocator");
    hr(g.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
       "test list");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {image, 0, state, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = image;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = output.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    hr(list->Close(), "close test copy");
    ID3D12CommandList *lists[]{list.Get()};
    g.queue()->ExecuteCommandLists(1, lists);
    g.sync();
    void *mapped{};
    D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    hr(output->Map(0, &range, &mapped), "map test readback");
    std::vector<uint32_t> result(size_t(desc.Width) * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(result.data() + y * desc.Width,
               static_cast<std::byte *>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch,
               desc.Width * 4);
    const D3D12_RANGE written{0, 0};
    output->Unmap(0, &written);
    return result;
}
void validation(ID3D12Device *device) {
    Com<ID3D12InfoQueue> queue;
    hr(device->QueryInterface(IID_PPV_ARGS(&queue)), "test validation queue");
    for (UINT64 i = 0; i < queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size{};
        hr(queue->GetMessage(i, nullptr, &size), "test message size");
        std::vector<std::byte> bytes(size);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
        hr(queue->GetMessage(i, message, &size), "test message");
        require(message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION &&
                    message->Severity != D3D12_MESSAGE_SEVERITY_ERROR,
                message->pDescription);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        const bool debug = argc == 2 && std::string_view(argv[1]) == "--debug-layer";
        const bool snapshot = argc == 3 && std::string_view(argv[1]) == "--snapshot";
        require(argc == 1 || debug || snapshot, "invalid depth stereo test arguments");
        if (debug) {
            Com<ID3D12Debug> layer;
            if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)))) {
                std::cout << "Depth stereo debug layer unavailable; NOT_RUN\n";
                return 77;
            }
            layer->EnableDebugLayer();
        }
        Com<IDXGIFactory4> factory;
        hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "test factory");
        Com<IDXGIAdapter1> warp;
        hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "test WARP");
        DXGI_ADAPTER_DESC1 adapter{};
        hr(warp->GetDesc1(&adapter), "test adapter");
        Renderer g;
        g.initialize(adapter.AdapterLuid, D3D_FEATURE_LEVEL_11_0, false);
        constexpr UINT width = 64, height = 16;
        std::vector<uint32_t> colors(width * height);
        std::vector<float> depths(width * height);
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width; ++x) {
                const auto i = y * width + x;
                colors[i] = 0xff000000 | (x * 3 + 1) | ((y * 11 + 3) << 8) | ((x + 30) << 16);
                depths[i] = x < 32 ? 2.f : 1.f;
            }
        auto color = upload_image(g, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, std::as_bytes(std::span(colors)));
        auto depth = upload_image(g, width, height, DXGI_FORMAT_R32_FLOAT, std::as_bytes(std::span(depths)));
        auto desc = color->GetDesc();
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        Com<ID3D12Resource> target;
        hr(g.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               nullptr, IID_PPV_ARGS(&target)),
           "test target");
        auto draw = [&](float eye, ID3D12Resource *z) {
            g.render_depth_eye(target.Get(), color.Get(), z, 64, eye, .05f, 10, 7, 7, 7);
            return read(g, target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        };
        require(draw(0, depth.Get()) == colors, "zero-baseline reconstruction is not pixel-identical");
        for (float eye : {-.0625f, .0625f}) {
            std::vector<uint32_t> expected(colors.size(), 0xff000000);
            std::vector<float> closest(colors.size(), 10.f);
            for (UINT y = 0; y < height; ++y)
                for (UINT x = 0; x < width; ++x) {
                    const auto i = y * width + x;
                    const auto destination = static_cast<int>(x) - static_cast<int>(64 * eye / depths[i]);
                    if (destination < 0 || destination >= static_cast<int>(width))
                        continue;
                    const auto d = y * width + destination;
                    if (depths[i] < closest[d]) {
                        closest[d] = depths[i];
                        expected[d] = colors[i];
                    }
                }
            require(draw(eye, depth.Get()) == expected,
                    "GPU stereo disparity, occlusion or disocclusion differs from analytic reference");
        }
        auto invalid_depths = depths;
        invalid_depths[0] = 0;
        invalid_depths[1] = -1;
        invalid_depths[2] = std::numeric_limits<float>::quiet_NaN();
        invalid_depths[3] = std::numeric_limits<float>::infinity();
        invalid_depths[4] = 10;
        auto invalid = upload_image(g, width, height, DXGI_FORMAT_R32_FLOAT, std::as_bytes(std::span(invalid_depths)));
        auto cleared = draw(0, invalid.Get());
        for (UINT i = 0; i < colors.size(); ++i)
            require(cleared[i] == (i < 5 ? 0xff000000 : colors[i]), "invalid depth was not discarded");
        rejects([&] { g.render_depth_eye(target.Get(), color.Get(), depth.Get(), 64, 0, .05f, 10, 7, 8, 7); });
        rejects([&] { g.render_depth_eye(target.Get(), color.Get(), depth.Get(), 64, 0, .05f, 10, 7, 7, 8); });
        rejects([&] { g.render_depth_eye(target.Get(), color.Get(), depth.Get(), NAN, 0, .05f, 10, 7, 7, 7); });
        rejects([&] { g.render_depth_eye(target.Get(), color.Get(), color.Get(), 64, 0, .05f, 10, 7, 7, 7); });
        auto actual_color = read(g, color.Get(), D3D12_RESOURCE_STATE_COMMON);
        require(actual_color == colors, "source color changed or was not restored to COMMON");
        auto actual_depth = read(g, depth.Get(), D3D12_RESOURCE_STATE_COMMON);
        require(memcmp(actual_depth.data(), depths.data(), depths.size() * sizeof(float)) == 0, "source depth changed");
        auto scene = owned_depth_scene(128, 72);
        require(scene.color.size() == 128 * 72 && scene.axial_depth.size() == scene.color.size(),
                "owned scene size mismatch");
        for (float z : scene.axial_depth)
            require(std::isfinite(z) && z > .05f && z < 50, "owned scene contains invalid depth");
        {
            DepthPreview preview(g);
            StereoReceiver receiver(g.queue(), preview.channel());
            require(receiver.description().technique == StereoTechnique::depth_reconstructed,
                    "preview lost reconstructed label");
            g.sync();
            auto pair = receiver.acquire();
            require(pair.has_value(), "preview did not publish a GPU pair");
            auto left = read(g, pair->left, D3D12_RESOURCE_STATE_COMMON);
            auto right = read(g, pair->right, D3D12_RESOURCE_STATE_COMMON);
            require(left != right, "preview eyes are identical");
            if (snapshot) {
                auto write = [&](const char *suffix, const std::vector<uint32_t> &image) {
                    std::ofstream file(std::string(argv[2]) + suffix, std::ios::binary);
                    file.exceptions(std::ios::badbit | std::ios::failbit);
                    file << "P6\n640 360\n255\n";
                    for (auto pixel : image) {
                        const char bytes[]{static_cast<char>(pixel), static_cast<char>(pixel >> 8),
                                           static_cast<char>(pixel >> 16)};
                        file.write(bytes, 3);
                    }
                };
                write("-left.ppm", left);
                write("-right.ppm", right);
            }
            receiver.release();
            g.sync();
        }
        if (debug)
            validation(g.device());
        std::cout << "PASS: GPU linear-depth stereo identity, analytic left/right disparity, nearest-depth occlusion, "
                     "black disocclusions, invalid-depth rejection, frame association, source restoration and owned "
                     "preview transport; game/headset NOT_RUN\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
