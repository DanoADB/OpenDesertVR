#include "odv/math/stereo.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace odv::math;
namespace {
int failures = 0;
void check(bool ok, const char *label) {
    if (!ok) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}
bool close(double a, double b, double epsilon = 1e-8) { return std::abs(a - b) < epsilon; }
template <typename Fn> void throws(Fn fn, const char *label) {
    bool caught = false;
    try {
        fn();
    } catch (const std::invalid_argument &) {
        caught = true;
    }
    check(caught, label);
}
} // namespace
int main() {
    const Pose center{};
    const auto left = eye_pose(center, -0.032);
    const auto right = eye_pose(center, 0.032);
    check(close(left.position.x, -0.032) && close(right.position.x, 0.032), "symmetric IPD");
    check(close(right.position.x - left.position.x, 0.064), "eye separation");
    const auto yaw90 = Quat{0, std::sqrt(0.5), 0, std::sqrt(0.5)};
    const auto rotated = rotate(yaw90, {1, 0, 0});
    check(close(rotated.x, 0) && close(rotated.z, -1), "orientation rotates local right");
    const Pose angled{yaw90, {2, 3, 4}};
    const auto eye = eye_pose(angled, 0.04);
    check(close(eye.position.x, 2) && close(eye.position.z, 3.96), "eye position follows head rotation");
    const auto view = view_matrix(angled);
    const auto cam = mul(view, {angled.position.x, angled.position.y, angled.position.z, 1});
    check(close(cam[0], 0) && close(cam[1], 0) && close(cam[2], 0) && close(cam[3], 1), "camera maps to origin");
    const Fov fov{-0.7, 0.7, 0.7, -0.7};
    const auto p = perspective_from_fov(fov, 0.1, 100);
    check(close(project_ndc(p, {0, 0, -0.1})[2], 0), "D3D depth near plane");
    check(close(project_ndc(p, {0, 0, -100})[2], 1), "D3D depth far plane");
    const auto vpLeft = view_matrix(left), vpRight = view_matrix(right);
    // Compare NDC x with a point at the world origin but in front of the eyes.
    const auto lnear = mul(vpLeft, {0, 0, -1, 1});
    const auto rnear = mul(vpRight, {0, 0, -1, 1});
    const auto lfar = mul(vpLeft, {0, 0, -10, 1});
    const auto rfar = mul(vpRight, {0, 0, -10, 1});
    const auto ndcLnear = project_ndc(p, {lnear[0], lnear[1], lnear[2]});
    const auto ndcRnear = project_ndc(p, {rnear[0], rnear[1], rnear[2]});
    const auto ndcLfar = project_ndc(p, {lfar[0], lfar[1], lfar[2]});
    const auto ndcRfar = project_ndc(p, {rfar[0], rfar[1], rfar[2]});
    const double nearDisparity = ndcLnear[0] - ndcRnear[0];
    const double farDisparity = ndcLfar[0] - ndcRfar[0];
    check(nearDisparity > 0 && farDisparity > 0 && nearDisparity > farDisparity * 5, "disparity falls with depth");
    throws([] { normalize({0, 0, 0, 0}); }, "zero quaternion rejected");
    check(close(normalize({0, 0, 0, 2}).w, 1), "non-unit quaternion normalized");
    throws([] { view_matrix({{}, {std::numeric_limits<double>::infinity(), 0, 0}}); }, "nonfinite camera rejected");
    throws([] { eye_pose({}, 0.2); }, "invalid IPD rejected");
    throws([] { perspective_from_fov({}, 0.1, 100); }, "invalid FOV rejected");
    throws([] { perspective_from_fov({-0.5, 0.5, 0.5, -0.5}, 1, 0.5); }, "invalid near/far rejected");
    throws([&] { project_ndc(p, {0, 0, 0}); }, "projection at eye origin rejected");
    throws([] { perspective_from_fov({-3, 3, 0.5, -0.5}, 0.1, 100); }, "wrapped FOV rejected");
    const auto asym = perspective_from_fov({-0.4, 0.8, 0.6, -0.3}, 0.1, 100);
    check(close(project_ndc(asym, {std::tan(-0.4), 0, -1})[0], -1), "asymmetric left edge");
    check(close(project_ndc(asym, {std::tan(0.8), 0, -1})[0], 1), "asymmetric right edge");
    check(close(project_ndc(asym, {0, std::tan(0.6), -1})[1], 1), "asymmetric top edge");
    std::cout << (failures ? "FAILED" : "PASSED") << " odv_math_tests\n";
    return failures ? 1 : 0;
}
