// SPDX-License-Identifier: MIT
// Owned native GPU control for the measured 40-byte indirect format. No game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "private_cbv_layout.hpp"
#include "private_indirect_signature.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <iostream>
#include <stdexcept>
#include <vector>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static void require(bool condition) {
    if (!condition)
        throw std::runtime_error("owned_private_indirect_control_failed");
}
static void checked(HRESULT result) { require(SUCCEEDED(result)); }
static D3D12_RESOURCE_DESC buffer(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
static Com<ID3D12Resource> upload(ID3D12Device *device, const void *data, SIZE_T bytes) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const auto desc = buffer(bytes);
    Com<ID3D12Resource> result;
    checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr, IID_PPV_ARGS(&result)));
    void *mapped = nullptr;
    const D3D12_RANGE no_read{};
    checked(result->Map(0, &no_read, &mapped));
    std::memcpy(mapped, data, bytes);
    result->Unmap(0, nullptr);
    return result;
}
static void unchanged(ID3D12Resource *resource, const void *original, SIZE_T bytes) {
    void *mapped = nullptr;
    const D3D12_RANGE range{0, bytes};
    checked(resource->Map(0, &range, &mapped));
    const bool equal = std::memcmp(mapped, original, bytes) == 0;
    const D3D12_RANGE no_write{};
    resource->Unmap(0, &no_write);
    require(equal);
}
static void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *target, D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &b);
}
struct Arguments {
    D3D12_INDEX_BUFFER_VIEW index;
    UINT token;
    D3D12_DRAW_INDEXED_ARGUMENTS draw;
};
static_assert(sizeof(Arguments) == 40 && offsetof(Arguments, token) == 16 && offsetof(Arguments, draw) == 20);
int main() {
    try {
        Com<ID3D12Debug> debug;
        const bool validation = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        if (validation)
            debug->EnableDebugLayer();
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        checked(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        Com<ID3D12InfoQueue> info;
        if (validation)
            checked(device.As(&info));
        const char shader[] = R"(
cbuffer DrawControl : register(b0,space99) { uint token; };
cbuffer Camera : register(b18,space35) { float4 offset; };
float4 vs(uint id:SV_VertexID):SV_Position {
 float2 p=float2((id<<1)&2,id&2);
 return float4(p*float2(2,-2)+float2(-1,1),0,1)+offset+
        float4((token==7 || token==8)?0:4,0,0,0);
}
float4 ps():SV_Target { return token==8?float4(0,1,0,1):float4(1,0,0,1); }
)";
        Com<ID3DBlob> vs, ps, errors;
        checked(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "vs", "vs_5_1", 0, 0, &vs, &errors));
        checked(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "ps", "ps_5_1", 0, 0, &ps, &errors));
        D3D12_DESCRIPTOR_RANGE1 range{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,           3, 17, 35,
                                      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0};
        D3D12_ROOT_PARAMETER1 params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Constants = {0, 99, 1};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[1].DescriptorTable = {1, &range};
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        root_desc.Desc_1_1 = {2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        std::array<Com<ID3D12RootSignature>, 2> roots;
        Com<ID3DBlob> blob;
        checked(D3D12SerializeVersionedRootSignature(&root_desc, &blob, &errors));
        checked(
            device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&roots[0])));
        odv::research::PrivateCbvLayout lifted;
        require(lifted.prepare(root_desc.Desc_1_1, 1, 18, 35, true) && lifted.private_index() == 2);
        blob.Reset();
        checked(D3D12SerializeVersionedRootSignature(lifted.description(), &blob, &errors));
        checked(
            device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&roots[1])));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pso_desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso_desc.BlendState.RenderTarget[0] = {FALSE,
                                               FALSE,
                                               D3D12_BLEND_ONE,
                                               D3D12_BLEND_ZERO,
                                               D3D12_BLEND_OP_ADD,
                                               D3D12_BLEND_ONE,
                                               D3D12_BLEND_ZERO,
                                               D3D12_BLEND_OP_ADD,
                                               D3D12_LOGIC_OP_NOOP,
                                               D3D12_COLOR_WRITE_ENABLE_ALL};
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.DepthStencilState.DepthEnable = FALSE;
        pso_desc.DepthStencilState.StencilEnable = FALSE;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                                D3D12_COMPARISON_FUNC_ALWAYS};
        pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;
        std::array<Com<ID3D12PipelineState>, 2> pipelines;
        for (UINT i = 0; i < 2; ++i) {
            pso_desc.pRootSignature = roots[i].Get();
            checked(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipelines[i])));
        }
        std::array<std::byte, 256> camera_bytes{};
        auto source = upload(device.Get(), camera_bytes.data(), camera_bytes.size());
        auto private_bytes = camera_bytes;
        const float offset = 2;
        std::memcpy(private_bytes.data(), &offset, sizeof(offset));
        auto private_camera = upload(device.Get(), private_bytes.data(), private_bytes.size());
        const std::array<UINT16, 3> indices{0, 1, 2};
        auto index = upload(device.Get(), indices.data(), sizeof(indices));
        Arguments a{{index->GetGPUVirtualAddress(), sizeof(indices), DXGI_FORMAT_R16_UINT}, 7, {3, 1, 0, 0, 0}};
        Arguments b = a;
        b.token = 8;
        std::array<std::byte, 112> argument_bytes{};
        argument_bytes.fill(std::byte{0xCD});
        std::memcpy(argument_bytes.data() + 16, &a, sizeof(a));
        std::memcpy(argument_bytes.data() + 56, &b, sizeof(b));
        auto arguments = upload(device.Get(), argument_bytes.data(), argument_bytes.size());
        const std::array<UINT, 4> count_bytes{0, 1, 2, 0xFA123456};
        auto counts = upload(device.Get(), count_bytes.data(), sizeof(count_bytes));
        D3D12_INDIRECT_ARGUMENT_DESC arg_desc[3]{};
        arg_desc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
        arg_desc[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        arg_desc[1].Constant = {0, 0, 1};
        arg_desc[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        const D3D12_COMMAND_SIGNATURE_DESC signature_desc{40, 3, arg_desc, 0};
        Com<ID3D12CommandSignature> original;
        checked(device->CreateCommandSignature(&signature_desc, roots[0].Get(), IID_PPV_ARGS(&original)));
        odv::research::NativeIndirectDescription metadata;
        require(odv::research::describe_indirect_signature(&signature_desc, roots[0].Get(), metadata));
        checked(original->SetPrivateData(odv::research::native_signature_metadata_guid, sizeof(metadata), &metadata));
        odv::research::PrivateIndirectSignature signature;
        require(signature.prepare(device.Get(), original.Get(), roots[0].Get(), roots[1].Get(), 2));
        Com<ID3D12DescriptorHeap> cbv_heap, rtv_heap;
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3,
                                             D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        checked(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&cbv_heap)));
        auto cpu = cbv_heap->GetCPUDescriptorHandleForHeapStart();
        const auto stride = device->GetDescriptorHandleIncrementSize(heap_desc.Type);
        for (UINT i = 0; i < 3; ++i) {
            const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{source->GetGPUVirtualAddress(), 256};
            device->CreateConstantBufferView(&cbv, {cpu.ptr + i * stride});
        }
        heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        checked(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap)));
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = texture.Height = 16;
        texture.DepthOrArraySize = texture.MipLevels = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        Com<ID3D12Resource> target;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
                                                D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)));
        const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.Get(), nullptr, rtv);
        struct Case {
            bool use_private, shift, no_count, reset;
            UINT maximum, count_offset;
            UINT expected;
        };
        const std::array<Case, 19> cases{{{false, false, false, false, 1, 4, 1},
                                          {true, true, false, false, 1, 4, 0},
                                          {false, false, false, false, 1, 4, 1},
                                          {false, false, false, false, 1, 0, 0},
                                          {false, false, false, false, 0, 4, 0},
                                          {false, false, false, false, 1, 8, 1},
                                          {false, false, false, false, 2, 8, 2},
                                          {false, false, false, true, 1, 4, 0},
                                          {true, true, false, true, 1, 4, 0},
                                          {false, false, false, true, 1, 0, 0},
                                          {false, false, false, true, 0, 4, 0},
                                          {false, false, true, false, 1, 0, 1},
                                          {true, false, false, false, 2, 8, 2},
                                          {true, false, false, false, 1, 0, 0},
                                          {true, false, false, false, 0, 4, 0},
                                          {true, false, false, true, 1, 0, 0},
                                          {true, false, false, true, 0, 4, 0},
                                          {true, false, false, false, 1, 8, 1},
                                          {true, false, true, false, 1, 0, 1}}};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        const auto readback_desc = buffer(cases.size() * 4096);
        Com<ID3D12Resource> readback;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        Com<ID3D12CommandAllocator> allocator;
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList> list;
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&list)));
        for (UINT i = 0; i < cases.size(); ++i) {
            const auto &c = cases[i];
            const UINT mode = c.use_private ? 1 : 0;
            list->SetGraphicsRootSignature(roots[mode].Get());
            list->SetPipelineState(pipelines[mode].Get());
            ID3D12DescriptorHeap *heaps[] = {cbv_heap.Get()};
            list->SetDescriptorHeaps(1, heaps);
            list->SetGraphicsRootDescriptorTable(1, cbv_heap->GetGPUDescriptorHandleForHeapStart());
            list->SetGraphicsRoot32BitConstant(0, c.reset ? 7 : 99, 0);
            if (c.use_private)
                list->SetGraphicsRootConstantBufferView(2, (c.shift ? private_camera : source)->GetGPUVirtualAddress());
            const D3D12_VIEWPORT viewport{0, 0, 16, 16, 0, 1};
            const D3D12_RECT scissor{0, 0, 16, 16};
            list->RSSetViewports(1, &viewport);
            list->RSSetScissorRects(1, &scissor);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const float black[4]{};
            list->ClearRenderTargetView(rtv, black, 0, nullptr);
            // Exactly one native call. Same GPU buffers and original byte offsets;
            // no CPU reading or rewriting of argument/count payloads for forwarding.
            list->ExecuteIndirect(c.use_private ? signature.replacement() : original.Get(), c.maximum, arguments.Get(),
                                  16, c.no_count ? nullptr : counts.Get(), c.no_count ? 0 : c.count_offset);
            if (c.reset) {
                list->ClearRenderTargetView(rtv, black, 0, nullptr);
                if (c.use_private)
                    list->SetGraphicsRootConstantBufferView(2, source->GetGPUVirtualAddress());
                // No root-constant rebind: native ExecuteIndirect must reset it to0.
                list->DrawInstanced(3, 1, 0, 0);
            }
            transition(list.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = {UINT64(i) * 4096, {DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 256}};
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = target.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&destination, 0, 0, 0, &from, nullptr);
            transition(list.Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        checked(list->Close());
        ID3D12CommandList *submission[] = {list.Get()};
        queue->ExecuteCommandLists(1, submission);
        Com<ID3D12Fence> fence;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        checked(queue->Signal(fence.Get(), 1));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(event != nullptr);
        checked(fence->SetEventOnCompletion(1, event));
        const DWORD waited = WaitForSingleObject(event, 10000);
        CloseHandle(event);
        require(waited == WAIT_OBJECT_0);
        require(fence->GetCompletedValue() >= 1 && fence->GetCompletedValue() != UINT64_MAX);
        void *mapped = nullptr;
        const D3D12_RANGE read_range{0, SIZE_T(readback_desc.Width)};
        checked(readback->Map(0, &read_range, &mapped));
        bool pixels = true;
        for (UINT i = 0; i < cases.size(); ++i)
            for (UINT y = 0; y < 16; ++y)
                for (UINT x = 0; x < 16; ++x) {
                    const auto *p = static_cast<const unsigned char *>(mapped) + i * 4096 + y * 256 + x * 4;
                    const auto expected = cases[i].expected;
                    pixels &= p[0] == (expected == 1 ? 255 : 0) && p[1] == (expected == 2 ? 255 : 0) && p[2] == 0 &&
                              p[3] == (expected ? 255 : 0);
                }
        const D3D12_RANGE no_write{};
        readback->Unmap(0, &no_write);
        require(pixels);
        unchanged(source.Get(), camera_bytes.data(), camera_bytes.size());
        unchanged(private_camera.Get(), private_bytes.data(), private_bytes.size());
        unchanged(arguments.Get(), argument_bytes.data(), argument_bytes.size());
        unchanged(counts.Get(), count_bytes.data(), sizeof(count_bytes));
        unchanged(index.Get(), indices.data(), sizeof(indices));
        if (info)
            for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T bytes = 0;
                checked(info->GetMessage(i, nullptr, &bytes));
                std::vector<std::byte> storage(bytes);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                checked(info->GetMessage(i, message, &bytes));
                require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                        message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION);
            }
        std::cout << "PASS: WARP exact40-byte IBV/root0/DrawIndexed private split-root GPU forwarding; "
                     "19 cases x256 pixels; GPU count0/1/2, max0/1/2, nonzero offsets, null count, "
                     "private camera change and restoration, root0 resets after nonzero/zero counts; "
                     "source/arguments/counts/indices exact; IBV null-reset GPU probe=NOT_RUN; debug="
                  << (validation ? "ENABLED" : "UNAVAILABLE") << "; game=NOT_RUN; headset=NOT_RUN\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
