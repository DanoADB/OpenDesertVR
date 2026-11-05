// SPDX-License-Identifier: MIT
#pragma once
#include "odv/math/stereo.hpp"
#include <vector>
namespace odv::stereo {
struct Vertex {
    float clip[4];
    float color[3];
};
std::vector<Vertex> vertices(const math::Pose &, const math::Fov &, double near_z, double far_z);
} // namespace odv::stereo
