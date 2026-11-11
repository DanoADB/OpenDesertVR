// SPDX-License-Identifier: MIT
#include "odv/d3d12/renderer.hpp"
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>

namespace odv::d3d {
void Renderer::render_depth_eye(ID3D12Resource *target, ID3D12Resource *color, ID3D12Resource *depth, float fx,
                                float eye, float near_z, float far_z, UINT64 color_frame, UINT64 depth_frame,
                                UINT64 projection_frame) {
    auto reject = [](bool bad) {
        if (bad)
            throw Failure(ErrorCode::configuration, "Invalid matched linear-depth stereo input");
    };
    reject(!target || !color || !depth || target == color || target == depth || color == depth || !color_frame ||
           color_frame != depth_frame || color_frame != projection_frame || !std::isfinite(fx) || fx <= 0 ||
           !std::isfinite(eye) || std::abs(eye) > .1f || !std::isfinite(near_z) || !std::isfinite(far_z) ||
           near_z <= 0 || far_z <= near_z);
    const auto desc = color->GetDesc();
    reject(!desc.Width || desc.Width > 2048 || !desc.Height || desc.Height > 2048 || fx > desc.Width * 16);
    for (auto *resource : {target, color, depth}) {
        const auto d = resource->GetDesc();
        Com<ID3D12Device> owner;
        hr(resource->GetDevice(IID_PPV_ARGS(&owner)), "depth stereo resource device");
        reject(owner.Get() != device_.Get() || d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
               d.Width != desc.Width || d.Height != desc.Height || d.DepthOrArraySize != 1 || d.MipLevels != 1 ||
               d.SampleDesc.Count != 1 ||
               d.Format != (resource == depth ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM) ||
               (resource == target && !(d.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) ||
               (resource != target && (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)));
    }
    sync();
    last_gpu_ms_.reset();
    ensure_depth(static_cast<UINT>(desc.Width), desc.Height);
    if (!depth_image_pipeline_) {
        constexpr char shader[] = R"(
Texture2D<float4> source_color : register(t0);
Texture2D<float> source_depth : register(t1);
cbuffer Params : register(b0) { uint width; uint height; float focal_x; float eye;
                              float near_z; float far_z; };
struct Pixel { float4 position : SV_Position; nointerpolation float4 color : COLOR; };
Pixel vs(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    uint2 pixel = uint2(instance % width, instance / width);
    float z = source_depth.Load(int3(pixel,0));
    Pixel o;
    o.color = float4(source_color.Load(int3(pixel,0)).rgb,1);
    if (!isfinite(z) || z < near_z || z >= far_z) { o.position = float4(2,2,0,1); return o; }
    float2 p = float2(pixel) + corners[vertex];
    p.x -= focal_x * eye / z;
    o.position = float4(2*p.x/width-1,1-2*p.y/height,(z-near_z)/(far_z-near_z),1);
    return o;
}
float4 ps(Pixel p) : SV_Target { return p.color; }
)";
        Com<ID3DBlob> vs, ps, errors, signature;
        hr(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "vs", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS,
                      0, &vs, &errors),
           "depth stereo vertex shader");
        hr(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "ps", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS,
                      0, &ps, &errors),
           "depth stereo pixel shader");
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants = {0, 0, 6};
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC root{2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        hr(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
           "depth stereo root serialization");
        hr(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                        IID_PPV_ARGS(&depth_image_root_)),
           "depth stereo root");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
        p.pRootSignature = depth_image_root_.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        auto &b = p.BlendState.RenderTarget[0];
        b.SrcBlend = b.SrcBlendAlpha = D3D12_BLEND_ONE;
        b.DestBlend = b.DestBlendAlpha = D3D12_BLEND_ZERO;
        b.BlendOp = b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        b.LogicOp = D3D12_LOGIC_OP_NOOP;
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE;
        p.DepthStencilState.DepthEnable = TRUE;
        p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        p.DepthStencilState.FrontFace = p.DepthStencilState.BackFace = {
            D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
        p.SampleMask = UINT_MAX;
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 1;
        p.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        p.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        p.SampleDesc.Count = 1;
        hr(device_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&depth_image_pipeline_)), "depth stereo pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 2;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&depth_image_srv_)), "depth stereo SRVs");
    }
    auto handle = depth_image_srv_->GetCPUDescriptorHandleForHeapStart();
    for (auto *resource : {color, depth}) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = resource->GetDesc().Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(resource, &srv, handle);
        handle.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(target, &rtv, rtv_->GetCPUDescriptorHandleForHeapStart());
    device_->CreateDepthStencilView(depth_.Get(), nullptr, dsv_->GetCPUDescriptorHandleForHeapStart());
    hr(allocator_->Reset(), "depth stereo allocator");
    hr(commands_->Reset(allocator_.Get(), depth_image_pipeline_.Get()), "depth stereo list");
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (UINT i = 0; i < 2; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition = {i == 0 ? color : depth, 0, D3D12_RESOURCE_STATE_COMMON,
                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    }
    commands_->ResourceBarrier(2, barriers);
    const auto target_handle = rtv_->GetCPUDescriptorHandleForHeapStart(),
               depth_handle = dsv_->GetCPUDescriptorHandleForHeapStart();
    const float black[]{0, 0, 0, 1};
    commands_->ClearRenderTargetView(target_handle, black, 0, nullptr);
    commands_->ClearDepthStencilView(depth_handle, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    commands_->OMSetRenderTargets(1, &target_handle, FALSE, &depth_handle);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap *heaps[]{depth_image_srv_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    commands_->SetGraphicsRootSignature(depth_image_root_.Get());
    commands_->SetGraphicsRootDescriptorTable(0, depth_image_srv_->GetGPUDescriptorHandleForHeapStart());
    struct Params {
        UINT width, height;
        float fx, eye, near_z, far_z;
    } parameters{static_cast<UINT>(desc.Width), desc.Height, fx, eye, near_z, far_z};
    static_assert(sizeof(Params) == 24);
    commands_->SetGraphicsRoot32BitConstants(1, 6, &parameters, 0);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(6, static_cast<UINT>(desc.Width) * desc.Height, 0, 0);
    for (auto &barrier : barriers)
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands_->ResourceBarrier(2, barriers);
    hr(commands_->Close(), "close depth stereo list");
    ID3D12CommandList *lists[]{commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    sync();
}
} // namespace odv::d3d
