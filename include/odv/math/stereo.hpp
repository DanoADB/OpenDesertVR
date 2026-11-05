#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace odv::math {
struct Vec3 {
    double x{}, y{}, z{};
};
struct Quat {
    double x{}, y{}, z{}, w{1.0};
};
struct Pose {
    Quat orientation{};
    Vec3 position{};
};
struct Fov {
    // OpenXR angles (radians), with +X right, +Y up, -Z forward.
    double left{}, right{}, up{}, down{};
};
using Matrix4 = std::array<double, 16>; // row-major, column-vector convention

Vec3 add(Vec3 a, Vec3 b) noexcept;
Vec3 subtract(Vec3 a, Vec3 b) noexcept;
Vec3 scale(Vec3 v, double s) noexcept;
double dot(Vec3 a, Vec3 b) noexcept;
Vec3 cross(Vec3 a, Vec3 b) noexcept;
double length(Vec3 a) noexcept;
Quat normalize(Quat q);
Vec3 rotate(Quat q, Vec3 v);
Pose eye_pose(const Pose& head, double signed_ipd_offset_m);
Matrix4 identity() noexcept;
Matrix4 view_matrix(const Pose& camera);
Matrix4 perspective_from_fov(const Fov& fov, double near_z, double far_z);
std::array<double, 4> mul(const Matrix4& m, std::array<double, 4> v) noexcept;
std::array<double, 3> project_ndc(const Matrix4& projection, Vec3 view_point);
} // namespace odv::math
