// SPDX-License-Identifier: MIT
#pragma once
#include "odv/d3d12/renderer.hpp"
#include <memory>
#include <string>

namespace odv::d3d {
// Transport origin is a label, never evidence that a game adapter is validated.
enum class StreamOrigin : UINT { owned_geometry = 1, game_candidate = 2 };
enum class StreamColor : UINT { linear = 1, srgb = 2 };
enum class StereoTechnique : UINT { independent_images = 1, depth_reconstructed = 2 };
struct StreamDescription {
    UINT width{}, height{};
    DXGI_FORMAT format{};
    LUID adapter{};
    DWORD producer_pid{};
    StreamOrigin origin{};
    StreamColor color{};
    StereoTechnique technique{};
};
struct FrameStamp {
    UINT64 left_source_frame{}, right_source_frame{};
};
struct StereoReadFrame {
    ID3D12Resource *left{}, *right{};
    FrameStamp stamp{};
    UINT64 sequence{};
};
enum class PublishResult { published, busy };

// Local, same-adapter, two-slot image-pair transport. It does not manufacture
// stereo or capture the desktop. The producer supplies eye textures and labels
// their technique; proving game-camera provenance is a separate adapter test.
class StereoPublisher {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    StereoPublisher(ID3D12CommandQueue *queue, UINT width, UINT height, StreamOrigin origin,
                    StreamColor color = StreamColor::linear,
                    StereoTechnique technique = StereoTechnique::independent_images);
    ~StereoPublisher();
    StereoPublisher(const StereoPublisher &) = delete;
    StereoPublisher &operator=(const StereoPublisher &) = delete;
    const std::wstring &channel() const;
    // Source states must be known by the caller on this exact direct queue.
    // Copies restore both inputs. Busy slots drop the pair without a GPU wait.
    PublishResult publish(ID3D12Resource *left, ID3D12Resource *right, D3D12_RESOURCE_STATES left_state,
                          D3D12_RESOURCE_STATES right_state, FrameStamp stamp);
    UINT64 dropped_pairs() const;
};

StreamDescription inspect_stereo_stream(const std::wstring &channel);
class StereoReceiver {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    StereoReceiver(ID3D12CommandQueue *queue, const std::wstring &channel);
    ~StereoReceiver();
    StereoReceiver(const StereoReceiver &) = delete;
    StereoReceiver &operator=(const StereoReceiver &) = delete;
    const StreamDescription &description() const;
    // One receiver and one outstanding lease. Incomplete pairs are not exposed.
    std::optional<StereoReadFrame> acquire();
    bool newer_available() const;
    // Submit all reads on the receiver's queue first, restoring COMMON. Do not
    // execute or retain commands referencing this lease after release().
    void release();
};
} // namespace odv::d3d
