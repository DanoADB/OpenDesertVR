// SPDX-License-Identifier: MIT
#pragma once
#include "odv/d3d12/stereo_stream.hpp"
#include <span>

namespace odv::d3d {
// Uploads a tightly packed, four-byte-per-pixel RGBA8 or R32_FLOAT owned image.
// Returns COMMON after completing the copy on graphics.queue().
Com<ID3D12Resource> upload_image(Renderer &graphics, UINT width, UINT height, DXGI_FORMAT format,
                                 std::span<const std::byte> pixels);
struct OwnedDepthScene {
    UINT width{}, height{};
    float focal_x{};
    std::vector<uint32_t> color;
    std::vector<float> axial_depth;
};
OwnedDepthScene owned_depth_scene(UINT width, UINT height);
class DepthPreview {
    Renderer &graphics_;
    Com<ID3D12Resource> color_, depth_, left_, right_;
    std::unique_ptr<StereoPublisher> publisher_;

  public:
    explicit DepthPreview(Renderer &graphics);
    const std::wstring &channel() const;
    void publish();
};
} // namespace odv::d3d
