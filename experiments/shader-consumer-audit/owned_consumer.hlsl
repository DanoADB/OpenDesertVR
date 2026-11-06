// SPDX-License-Identifier: MIT
// Original compile/reflection fixture only. Never injected into a game or run.
cbuffer OwnedScene : register(b0) {
    uint ownedFrame : packoffset(c2.x);
    float3 ownedPosition : packoffset(c8.x);
    float4x4 ownedView : packoffset(c62);
    float4x4 ownedProjection : packoffset(c78);
};
RWStructuredBuffer<float4> ownedOutput : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 dispatch : SV_DispatchThreadID) {
    ownedOutput[dispatch.x] = mul(ownedProjection, mul(ownedView, float4(ownedPosition, 1))) + ownedFrame;
}
