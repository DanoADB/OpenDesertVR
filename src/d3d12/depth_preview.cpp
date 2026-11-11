// SPDX-License-Identifier: MIT
#include "odv/d3d12/depth_preview.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace odv::d3d {
Com<ID3D12Resource> upload_image(Renderer &g, UINT width, UINT height, DXGI_FORMAT format,
                                 std::span<const std::byte> pixels) {
    if (!width || width > 2048 || !height || height > 2048 || pixels.size() != size_t(width) * height * 4 ||
        (format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_R32_FLOAT))
        throw Failure(ErrorCode::configuration, "Invalid owned image upload");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Com<ID3D12Resource> image, upload;
    hr(g.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                           IID_PPV_ARGS(&image)),
       "owned image allocation");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes{};
    g.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes;
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr(g.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                                           nullptr, IID_PPV_ARGS(&upload)),
       "owned image upload allocation");
    void *mapped{};
    const D3D12_RANGE read{0, 0};
    hr(upload->Map(0, &read, &mapped), "map owned image");
    for (UINT y = 0; y < height; ++y)
        memcpy(static_cast<std::byte *>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch,
               pixels.data() + size_t(y) * width * 4, size_t(width) * 4);
    upload->Unmap(0, nullptr);
    Com<ID3D12CommandAllocator> allocator;
    Com<ID3D12GraphicsCommandList> list;
    hr(g.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
       "owned upload allocator");
    hr(g.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
       "owned upload list");
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    destination.pResource = image.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {image.Get(), 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
    list->ResourceBarrier(1, &barrier);
    hr(list->Close(), "close owned upload");
    ID3D12CommandList *lists[]{list.Get()};
    g.queue()->ExecuteCommandLists(1, lists);
    g.sync();
    return image;
}
OwnedDepthScene owned_depth_scene(UINT width, UINT height) {
    if (!width || width > 2048 || !height || height > 2048)
        throw Failure(ErrorCode::configuration, "Invalid owned scene dimensions");
    OwnedDepthScene out{width, height, width * .72f};
    out.color.resize(size_t(width) * height);
    out.axial_depth.resize(out.color.size());
    // Original procedural courtyard. Ray parameter is axial Z in metres because
    // direction.z is one. The color and depth come from the very same hit.
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const float dx = (x + .5f - width * .5f) / out.focal_x, dy = (height * .5f - y - .5f) / out.focal_x;
            float z = 48, shade = .85f;
            uint32_t rgb = 0x00cba87b; // RGBA bytes: blue sky.
            if (dy < -.001f) {
                const float hit = -1.6f / dy;
                if (hit < z) {
                    z = hit;
                    const auto tile = static_cast<int>(std::floor(dx * z)) + static_cast<int>(std::floor(z));
                    rgb = (tile & 1) ? 0x00486e7a : 0x0067939c;
                    shade = .45f + .5f * std::exp(-z * .035f);
                }
            }
            // Stone pillars and gateway, plus a near amber sphere and far teal orb.
            auto box = [&](float minx, float maxx, float miny, float maxy, float minz, float maxz) {
                float enter = .05f, exit = z;
                const float directions[]{dx, dy, 1}, lows[]{minx, miny - 1.6f, minz}, highs[]{maxx, maxy - 1.6f, maxz};
                for (int axis = 0; axis < 3; ++axis) {
                    if (std::abs(directions[axis]) < 1e-7f) {
                        if (lows[axis] > 0 || highs[axis] < 0)
                            return;
                        continue;
                    }
                    float a = lows[axis] / directions[axis], b = highs[axis] / directions[axis];
                    if (a > b)
                        std::swap(a, b);
                    enter = (std::max)(enter, a);
                    exit = (std::min)(exit, b);
                    if (enter > exit)
                        return;
                }
                if (enter < z) {
                    z = enter;
                    rgb = 0x008fa6b4;
                    shade = .65f + .16f * std::sin(z * 2 + dx * 9);
                }
            };
            for (float side : {-1.f, 1.f}) {
                box(side * 2.1f - .28f, side * 2.1f + .28f, 0, 3.5f, 5, 5.6f);
                box(side * 4.1f - .4f, side * 4.1f + .4f, 0, 4.5f, 11, 12);
            }
            box(-2.4f, 2.4f, 3.2f, 3.65f, 5, 5.6f);
            auto sphere = [&](float cx, float cy, float cz, float radius, uint32_t material) {
                const float ox = -cx, oy = 1.6f - cy, oz = -cz, a = dx * dx + dy * dy + 1;
                const float b = ox * dx + oy * dy + oz, c = ox * ox + oy * oy + oz * oz - radius * radius,
                            discriminant = b * b - a * c;
                if (discriminant < 0)
                    return;
                const float hit = (-b - std::sqrt(discriminant)) / a;
                if (hit > .05f && hit < z) {
                    z = hit;
                    rgb = material;
                    const float nx = (dx * z - cx) / radius, ny = (1.6f + dy * z - cy) / radius, nz = (z - cz) / radius;
                    shade = .25f + .75f * (std::max)(0.f, -nx * .4f + ny * .65f - nz * .6f);
                }
            };
            sphere(-.55f, 1, 2.5f, .58f, 0x002a9ee8);
            sphere(1.1f, 1.2f, 7, .85f, 0x009fa43c);
            uint32_t color = 0xff000000;
            for (UINT channel = 0; channel < 3; ++channel)
                color |= static_cast<uint32_t>(std::clamp(((rgb >> (channel * 8)) & 255) * shade, 0.f, 255.f))
                         << (channel * 8);
            const size_t index = size_t(y) * width + x;
            out.color[index] = color;
            out.axial_depth[index] = z;
        }
    return out;
}
DepthPreview::DepthPreview(Renderer &g) : graphics_(g) {
    auto scene = owned_depth_scene(640, 360);
    color_ =
        upload_image(g, scene.width, scene.height, DXGI_FORMAT_R8G8B8A8_UNORM, std::as_bytes(std::span(scene.color)));
    depth_ =
        upload_image(g, scene.width, scene.height, DXGI_FORMAT_R32_FLOAT, std::as_bytes(std::span(scene.axial_depth)));
    auto desc = color_->GetDesc();
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    for (auto *eye : {std::addressof(left_), std::addressof(right_)})
        hr(g.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               nullptr, IID_PPV_ARGS(eye->ReleaseAndGetAddressOf())),
           "owned reconstructed eye");
    g.render_depth_eye(left_.Get(), color_.Get(), depth_.Get(), scene.focal_x, -.032f, .05f, 50, 1, 1, 1);
    g.render_depth_eye(right_.Get(), color_.Get(), depth_.Get(), scene.focal_x, .032f, .05f, 50, 1, 1, 1);
    publisher_ = std::make_unique<StereoPublisher>(g.queue(), scene.width, scene.height, StreamOrigin::owned_geometry,
                                                   StreamColor::srgb, StereoTechnique::depth_reconstructed);
    publish();
}
const std::wstring &DepthPreview::channel() const { return publisher_->channel(); }
void DepthPreview::publish() {
    publisher_->publish(left_.Get(), right_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_RENDER_TARGET, {1, 1});
}
} // namespace odv::d3d
