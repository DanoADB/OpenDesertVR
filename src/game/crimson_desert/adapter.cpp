// SPDX-License-Identifier: MIT
#include "odv/game/adapter.hpp"
namespace odv::game {
Json CrimsonDesertAdapter::probe_capabilities() const {
    return {{"schema_version", 1},
            {"adapter", "CrimsonDesert"},
            {"active", false},
            {"integration", "Unsupported"},
            {"build", "Unverified"},
            {"theater", "Unverified"},
            {"native_stereo", "Unverified"},
            {"reconstructed_depth", "Unverified"},
            {"head_tracking", "Unverified"},
            {"motion_control", "Unverified"},
            {"per_eye_camera", "Unverified"},
            {"depth", "Unverified"},
            {"motion_vectors", "Unverified"},
            {"hud", "Unverified"},
            {"frame_ownership", "Unverified"},
            {"adapter_compatibility", "Unverified"}};
}
} // namespace odv::game
