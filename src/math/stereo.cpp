#include "odv/math/stereo.hpp"
#include <algorithm>
#include <limits>

namespace odv::math {
Vec3 add(Vec3 a, Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 subtract(Vec3 a, Vec3 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 scale(Vec3 v, double s) noexcept { return {v.x*s,v.y*s,v.z*s}; }
double dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double length(Vec3 v) noexcept { return std::sqrt(dot(v,v)); }
Quat normalize(Quat q) {
    const double s=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if(!std::isfinite(s) || s<1e-20) throw std::invalid_argument("invalid quaternion");
    const double inv=1.0/std::sqrt(s);
    return {q.x*inv,q.y*inv,q.z*inv,q.w*inv};
}
Vec3 rotate(Quat q, Vec3 v) {
    q=normalize(q);
    const Vec3 u{q.x,q.y,q.z};
    return add(v, add(scale(cross(u,v),2*q.w), scale(cross(u,cross(u,v)),2)));
}
Pose eye_pose(const Pose& head, double signed_ipd_offset_m) {
    if(!std::isfinite(signed_ipd_offset_m) || std::abs(signed_ipd_offset_m)>0.15)
        throw std::invalid_argument("eye offset exceeds 15 cm");
    return {normalize(head.orientation),add(head.position, rotate(head.orientation,{signed_ipd_offset_m,0,0}))};
}
Matrix4 identity() noexcept {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}
Matrix4 view_matrix(const Pose& camera) {
    const auto q=normalize(camera.orientation);
    const Vec3 x=rotate(q,{1,0,0}),y=rotate(q,{0,1,0}),z=rotate(q,{0,0,1});
    const Vec3 p=camera.position;
    return {x.x,x.y,x.z,-dot(x,p),
            y.x,y.y,y.z,-dot(y,p),
            z.x,z.y,z.z,-dot(z,p),
            0,0,0,1};
}
Matrix4 perspective_from_fov(const Fov& fov, double n, double f) {
    if (!(std::isfinite(n)&&std::isfinite(f)&&n>0&&f>n))
        throw std::invalid_argument("invalid clipping planes");
    const double l=std::tan(fov.left),r=std::tan(fov.right);
    const double u=std::tan(fov.up),d=std::tan(fov.down);
    if (!(std::isfinite(l)&&std::isfinite(r)&&std::isfinite(u)&&std::isfinite(d)&&l<r&&d<u))
        throw std::invalid_argument("invalid OpenXR field of view");
    // Right-handed (-Z forward), D3D/Vulkan style NDC depth [0,1].
    const double sx=2/(r-l),sy=2/(u-d);
    const double ox=(r+l)/(r-l),oy=(u+d)/(u-d);
    return {sx,0,ox,0,
            0,sy,oy,0,
            0,0,-f/(f-n),-f*n/(f-n),
            0,0,-1,0};
}
std::array<double,4> mul(const Matrix4& m,std::array<double,4> v) noexcept {
    return {m[0]*v[0]+m[1]*v[1]+m[2]*v[2]+m[3]*v[3],
            m[4]*v[0]+m[5]*v[1]+m[6]*v[2]+m[7]*v[3],
            m[8]*v[0]+m[9]*v[1]+m[10]*v[2]+m[11]*v[3],
            m[12]*v[0]+m[13]*v[1]+m[14]*v[2]+m[15]*v[3]};
}
std::array<double,3> project_ndc(const Matrix4& projection,Vec3 p) {
    const auto v=mul(projection,{p.x,p.y,p.z,1});
    if(!std::isfinite(v[3]) || std::abs(v[3])<1e-12)
        throw std::invalid_argument("projection at eye origin");
    return {v[0]/v[3],v[1]/v[3],v[2]/v[3]};
}
} // namespace odv::math
