// SPDX-License-Identifier: MIT
#include "odv/stereo/scene.hpp"
namespace odv::stereo {
std::vector<Vertex> vertices(const math::Pose &eye, const math::Fov &fov, double n, double f) {
    const auto view = math::view_matrix(eye), projection = math::perspective_from_fov(fov, n, f);
    constexpr int indices[] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                               3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    constexpr double depths[] = {1, 3, 8};
    constexpr double centers[] = {-0.4, 0, 1.6};
    constexpr float colors[][3] = {{1, 0.15f, 0.1f}, {0.1f, 1, 0.25f}, {0.15f, 0.4f, 1}};
    std::vector<Vertex> result;
    for (int cube = 0; cube < 3; ++cube) {
        const double s = depths[cube] * 0.1;
        const math::Vec3 corners[] = {{-s, -s, s},  {s, -s, s},  {s, s, s},  {-s, s, s},
                                      {-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s}};
        for (int index : indices) {
            auto p = corners[index];
            p.x += centers[cube];
            p.z -= depths[cube];
            const auto clip = math::mul(projection, math::mul(view, {p.x, p.y, p.z, 1}));
            Vertex vertex{};
            for (int i = 0; i < 4; ++i)
                vertex.clip[i] = static_cast<float>(clip[i]);
            for (int i = 0; i < 3; ++i)
                vertex.color[i] = colors[cube][i] * (0.65f + 0.35f * static_cast<float>(index % 3) / 2);
            result.push_back(vertex);
        }
    }
    return result;
}
} // namespace odv::stereo
