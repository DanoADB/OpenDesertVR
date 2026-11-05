// SPDX-License-Identifier: MIT
#include "odv/d3d12/renderer.hpp"
#include "odv/openxr/lifecycle.hpp"
#include <cstring>
#include <d3dcompiler.h>
#include <sstream>

namespace odv::d3d {
void hr(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
        throw Failure(result == E_OUTOFMEMORY ? ErrorCode::allocation : ErrorCode::device, message.str());
    }
}
Renderer::~Renderer() {
    if (queue_ && fence_ && event_) {
        try {
            sync();
        } catch (...) {
        }
    }
    if (event_)
        CloseHandle(event_);
}
void Renderer::initialize(LUID luid, D3D_FEATURE_LEVEL level, bool debug) {
    if (debug) {
        Com<ID3D12Debug> layer;
        hr(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)), "D3D12 debug layer (install Graphics Tools)");
        layer->EnableDebugLayer();
    }
    Com<IDXGIFactory4> factory;
    hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    Com<IDXGIAdapter1> adapter;
    const auto result = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
    xr::validate({true, true, true, SUCCEEDED(result), true, true});
    hr(D3D12CreateDevice(adapter.Get(), level, IID_PPV_ARGS(&device_)), "D3D12 device");
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr(device_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue_)), "command queue");
    hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)), "command allocator");
    hr(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                  IID_PPV_ARGS(&commands_)),
       "command list");
    hr(commands_->Close(), "close initial list");
    hr(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "fence allocation");
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_)
        throw Failure(ErrorCode::allocation, "fence event allocation failed");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv_)), "RTV heap allocation");
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsv_)), "DSV heap allocation");
    D3D12_HEAP_PROPERTIES props{};
    props.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 65536;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr(device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                                        nullptr, IID_PPV_ARGS(&upload_)),
       "vertex allocation");
}
void Renderer::pipeline(DXGI_FORMAT format) {
    const char *shader = R"(
struct Input { float4 position : POSITION; float3 color : COLOR; };
struct Pixel { float4 position : SV_POSITION; float3 color : COLOR; };
Pixel vs(Input i) { Pixel o; o.position=i.position; o.color=i.color; return o; }
float4 ps(Pixel i) : SV_TARGET { return float4(i.color,1); }
)";
    Com<ID3DBlob> vs, ps, error, signature;
    hr(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "vs", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                  &vs, &error),
       "vertex shader");
    hr(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "ps", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                  &ps, &error),
       "pixel shader");
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error), "serialize root");
    hr(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_)),
       "root signature");
    D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root_.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    p.InputLayout = {input, 2};
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    auto &blend = p.BlendState.RenderTarget[0];
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    p.DepthStencilState.DepthEnable = TRUE;
    p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    p.DepthStencilState.FrontFace = p.DepthStencilState.BackFace = {
        D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = format;
    p.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    p.SampleDesc.Count = 1;
    hr(device_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline_)), "graphics pipeline");
}
void Renderer::sync() {
    hr(queue_->Signal(fence_.Get(), ++serial_), "queue signal");
    if (fence_->GetCompletedValue() == UINT64_MAX)
        throw Failure(ErrorCode::device, "D3D12 device removed");
    if (fence_->GetCompletedValue() < serial_) {
        hr(fence_->SetEventOnCompletion(serial_, event_), "fence event");
        // Check removal without abandoning live command resources on a timeout.
        DWORD wait_result = WAIT_TIMEOUT;
        while ((wait_result = WaitForSingleObject(event_, 1000)) == WAIT_TIMEOUT)
            hr(device_->GetDeviceRemovedReason(), "GPU wait/device removal");
        if (wait_result != WAIT_OBJECT_0)
            throw Failure(ErrorCode::device, "GPU fence wait failed");
        hr(device_->GetDeviceRemovedReason(), "GPU completion");
    }
}
void Renderer::render(ID3D12Resource *target, DXGI_FORMAT format, UINT width, UINT height,
                      const std::vector<stereo::Vertex> &vertices) {
    sync();
    if (!depth_ || width != width_ || height != height_) {
        depth_.Reset();
        width_ = width;
        height_ = height;
        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1;
        hr(device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                            &clear, IID_PPV_ARGS(&depth_)),
           "depth allocation");
    }
    const size_t size = vertices.size() * sizeof(stereo::Vertex);
    if (size > 65536)
        throw Failure(ErrorCode::allocation, "vertex upload capacity exceeded");
    void *mapped{};
    D3D12_RANGE read{0, 0};
    hr(upload_->Map(0, &read, &mapped), "map vertices");
    memcpy(mapped, vertices.data(), size);
    upload_->Unmap(0, nullptr);
    hr(allocator_->Reset(), "reset allocator");
    hr(commands_->Reset(allocator_.Get(), pipeline_.Get()), "reset list");
    D3D12_RENDER_TARGET_VIEW_DESC view{};
    view.Format = format;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto rtv = rtv_->GetCPUDescriptorHandleForHeapStart(), dsv = dsv_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(target, &view, rtv);
    device_->CreateDepthStencilView(depth_.Get(), nullptr, dsv);
    // OpenXR D3D12 color images are acquired/released in RENDER_TARGET state.
    // No COMMON/PRESENT transition is permitted or necessary for these images.
    const float color[] = {0.025f, 0.035f, 0.055f, 1};
    commands_->ClearRenderTargetView(rtv, color, 0, nullptr);
    commands_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    commands_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    commands_->SetGraphicsRootSignature(root_.Get());
    const D3D12_VERTEX_BUFFER_VIEW vb{upload_->GetGPUVirtualAddress(), static_cast<UINT>(size), sizeof(stereo::Vertex)};
    commands_->IASetVertexBuffers(0, 1, &vb);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
    hr(commands_->Close(), "close list");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    sync(); // Simplicity over throughput: GPU complete before image release or buffer reuse.
}
} // namespace odv::d3d
