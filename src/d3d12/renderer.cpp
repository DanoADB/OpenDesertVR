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
    // Timestamp support is optional; failure to obtain a frequency leaves the
    // metric NOT_RUN. Once enabled, allocation/device failures remain fatal.
    if (SUCCEEDED(queue_->GetTimestampFrequency(&timestamp_frequency_)) && timestamp_frequency_ != 0) {
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query.Count = 2;
        hr(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamp_heap_)), "timestamp query heap");
        props.Type = D3D12_HEAP_TYPE_READBACK;
        buffer.Width = 2 * sizeof(UINT64);
        hr(device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                            nullptr, IID_PPV_ARGS(&timestamp_readback_)),
           "timestamp readback allocation");
    }
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
    last_gpu_ms_.reset();
    if (timestamp_heap_)
        commands_->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
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
    if (timestamp_heap_) {
        commands_->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        commands_->ResolveQueryData(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timestamp_readback_.Get(),
                                    0);
    }
    hr(commands_->Close(), "close list");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    sync(); // Simplicity over throughput: GPU complete before image release or buffer reuse.
    if (timestamp_heap_) {
        void *data{};
        const D3D12_RANGE range{0, 2 * sizeof(UINT64)};
        hr(timestamp_readback_->Map(0, &range, &data), "read GPU timestamps");
        UINT64 values[2]{};
        memcpy(values, data, sizeof(values));
        const D3D12_RANGE written{0, 0};
        timestamp_readback_->Unmap(0, &written);
        if (values[1] >= values[0])
            last_gpu_ms_ =
                1000.0 * static_cast<double>(values[1] - values[0]) / static_cast<double>(timestamp_frequency_);
    }
}
void Renderer::render_texture(ID3D12Resource *target, DXGI_FORMAT format, UINT width, UINT height,
                              ID3D12Resource *source, bool source_srgb) {
    if (!target || !source || target == source || !width || !height)
        throw Failure(ErrorCode::configuration, "Invalid stream rendering target");
    const auto source_desc = source->GetDesc();
    if (source_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        source_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || source_desc.DepthOrArraySize != 1 ||
        source_desc.MipLevels != 1 || source_desc.SampleDesc.Count != 1)
        throw Failure(ErrorCode::configuration, "Unsupported stream image descriptor");
    Com<ID3D12Device> source_device;
    hr(source->GetDevice(IID_PPV_ARGS(&source_device)), "stream image device");
    if (source_device.Get() != device_.Get())
        throw Failure(ErrorCode::adapter, "Stream image must be opened on the renderer device");
    sync();               // Previous descriptor/allocator use must finish before reuse.
    last_gpu_ms_.reset(); // Texture import timing is not yet instrumented.
    if (!texture_pipeline_ || texture_format_ != format) {
        constexpr char shader[] = R"(
Texture2D<float4> image : register(t0);
SamplerState image_sampler : register(s0);
cbuffer Encoding : register(b0) { uint decode_srgb; };
struct Pixel { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Pixel vs(uint id : SV_VertexID) {
    Pixel o; o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv.x * 2 - 1, 1 - o.uv.y * 2, 0, 1); return o;
}
float linear_channel(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float4 ps(Pixel i) : SV_Target {
    float3 c = image.Sample(image_sampler, i.uv).rgb;
    if (decode_srgb) c = float3(linear_channel(c.r), linear_channel(c.g), linear_channel(c.b));
    return float4(c, 1);
})";
        Com<ID3DBlob> vs, ps, errors, serialized;
        hr(D3DCompile(shader, sizeof(shader) - 1, "owned-stream-copy", nullptr, nullptr, "vs", "vs_5_0",
                      D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &errors),
           "stream vertex shader");
        hr(D3DCompile(shader, sizeof(shader) - 1, "owned-stream-copy", nullptr, nullptr, "ps", "ps_5_0",
                      D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &errors),
           "stream pixel shader");
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants = {0, 0, 1};
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root{2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        hr(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors),
           "serialize stream root signature");
        hr(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                        IID_PPV_ARGS(&texture_root_)),
           "stream root signature");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
        p.pRootSignature = texture_root_.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        p.BlendState.RenderTarget[0] = {FALSE,
                                        FALSE,
                                        D3D12_BLEND_ONE,
                                        D3D12_BLEND_ZERO,
                                        D3D12_BLEND_OP_ADD,
                                        D3D12_BLEND_ONE,
                                        D3D12_BLEND_ZERO,
                                        D3D12_BLEND_OP_ADD,
                                        D3D12_LOGIC_OP_NOOP,
                                        D3D12_COLOR_WRITE_ENABLE_ALL};
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE;
        p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        p.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                         D3D12_COMPARISON_FUNC_ALWAYS};
        p.DepthStencilState.BackFace = p.DepthStencilState.FrontFace;
        p.SampleMask = UINT_MAX;
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 1;
        p.RTVFormats[0] = format;
        p.SampleDesc.Count = 1;
        hr(device_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&texture_pipeline_)), "stream texture pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&texture_srv_)), "stream SRV heap");
        texture_format_ = format;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = source_desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(source, &srv, texture_srv_->GetCPUDescriptorHandleForHeapStart());
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = format;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(target, &rtv, rtv_->GetCPUDescriptorHandleForHeapStart());
    hr(allocator_->Reset(), "stream render allocator reset");
    hr(commands_->Reset(allocator_.Get(), texture_pipeline_.Get()), "stream render list reset");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {source, 0, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    commands_->ResourceBarrier(1, &barrier);
    const auto handle = rtv_->GetCPUDescriptorHandleForHeapStart();
    commands_->OMSetRenderTargets(1, &handle, FALSE, nullptr);
    const float black[]{0, 0, 0, 1};
    commands_->ClearRenderTargetView(handle, black, 0, nullptr);
    const double scale =
        (std::min)(static_cast<double>(width) / source_desc.Width, static_cast<double>(height) / source_desc.Height);
    const float content_width = static_cast<float>(source_desc.Width * scale),
                content_height = static_cast<float>(source_desc.Height * scale);
    D3D12_VIEWPORT viewport{
        (width - content_width) / 2, (height - content_height) / 2, content_width, content_height, 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap *heaps[]{texture_srv_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    commands_->SetGraphicsRootSignature(texture_root_.Get());
    commands_->SetGraphicsRootDescriptorTable(0, texture_srv_->GetGPUDescriptorHandleForHeapStart());
    commands_->SetGraphicsRoot32BitConstant(1, source_srgb ? 1 : 0, 0);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(3, 1, 0, 0);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands_->ResourceBarrier(1, &barrier);
    hr(commands_->Close(), "close stream render list");
    ID3D12CommandList *lists[]{commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    sync();
}
} // namespace odv::d3d
