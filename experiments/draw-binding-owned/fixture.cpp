// SPDX-License-Identifier: MIT
// Original WARP geometry/readback control. No game, XR or capture-tool dependency.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <array>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <windows.h>
#include <wrl/client.h>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("owned_draw_graphics_failure");
}
static constexpr UINT width = 64, height = 32;
static constexpr char shader[] = R"(
cbuffer VertexControl : register(b2, space3) { float4 clipOffset; };
cbuffer PixelControl : register(b5, space7) { float4 color; };
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1) + clipOffset;
}
float4 ps() : SV_Target { return color; }
)";
static void write_blob(const std::filesystem::path &path, ID3DBlob *blob) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char *>(blob->GetBufferPointer()), blob->GetBufferSize());
    if (!out)
        throw std::runtime_error("owned_shader_write_failed");
}
static D3D12_RESOURCE_DESC buffer_desc(UINT64 size) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
static void barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &b);
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 2 && (argc != 3 || std::wstring_view(argv[2]) != L"--submit-fourth"))
            throw std::runtime_error("provide_fresh_output_directory");
        const bool submit_fourth = argc == 3;
        const std::filesystem::path output(argv[1]);
        if (!std::filesystem::is_directory(output) || !std::filesystem::is_empty(output))
            throw std::runtime_error("output_must_be_existing_empty_directory");
        // This baseline must run without a capture DLL beside the executable.
        wchar_t module[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, module, MAX_PATH))
            throw std::runtime_error("module_path_failed");
        const auto executable_dir = std::filesystem::path(module).parent_path();
        for (const auto *name : {L"dxgi.dll", L"d3d12.dll", L"ReShade64.dll"})
            if (std::filesystem::exists(executable_dir / name))
                throw std::runtime_error("baseline_local_graphics_proxy_present");
        Com<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC q{};
        check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
        Com<ID3DBlob> vs, ps, errors;
        check(D3DCompile(shader, sizeof(shader) - 1, "owned_control", nullptr, nullptr, "vs", "vs_5_1",
                         D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &errors));
        check(D3DCompile(shader, sizeof(shader) - 1, "owned_control", nullptr, nullptr, "ps", "ps_5_1",
                         D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &errors));
        write_blob(output / "owned.vs.cso", vs.Get());
        write_blob(output / "owned.ps.cso", ps.Get());
        std::array<Com<ID3D12RootSignature>, 2> roots;
        std::array<Com<ID3D12PipelineState>, 2> pipelines;
        for (UINT mode = 0; mode < 2; ++mode) {
            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            D3D12_ROOT_PARAMETER params[2]{};
            for (UINT i = 0; i < 2; ++i) {
                params[i].ShaderVisibility = i == 0 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
                const UINT reg = i == 0 ? 2 : 5, space = i == 0 ? 3 : 7;
                if (mode == 0) {
                    params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    params[i].Descriptor = {reg, space};
                } else {
                    ranges[i] = {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, reg, space, 0};
                    params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    params[i].DescriptorTable = {1, &ranges[i]};
                }
            }
            D3D12_ROOT_SIGNATURE_DESC rd{};
            rd.NumParameters = 2;
            rd.pParameters = params;
            Com<ID3DBlob> serialized;
            check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
            check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                              IID_PPV_ARGS(&roots[mode])));
            D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
            p.pRootSignature = roots[mode].Get();
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
            p.SampleMask = UINT_MAX;
            p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            p.RasterizerState.DepthClipEnable = TRUE;
            p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            p.NumRenderTargets = 1;
            p.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            p.SampleDesc.Count = 1;
            check(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipelines[mode])));
        }
        std::array<Com<ID3D12Resource>, 8> constants;
        const float values[8][4] = {{0, 0, 0, 0}, {1, 0, 0, 1}, {0, 0, 0, 0}, {0, 1, 0, 1},
                                    {0, 0, 0, 0}, {0, 0, 1, 1}, {0, 0, 0, 0}, {1, 1, 0, 1}};
        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        for (UINT i = 0; i < constants.size(); ++i) {
            const auto d = buffer_desc(256ULL * (i + 1));
            check(device->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                  nullptr, IID_PPV_ARGS(&constants[i])));
            const auto name = L"OwnedControl" + std::to_wstring(i);
            check(constants[i]->SetName(name.c_str()));
            void *mapped = nullptr;
            D3D12_RANGE empty{0, 0};
            check(constants[i]->Map(0, &empty, &mapped));
            std::memset(mapped, 0, static_cast<size_t>(d.Width));
            std::memcpy(mapped, values[i], sizeof(values[i]));
            // Deliberately persistently mapped; no writes after initialization.
        }
        Com<ID3D12DescriptorHeap> cbheap;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 4;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&cbheap)));
        const auto cstride = device->GetDescriptorHandleIncrementSize(hd.Type);
        const auto cpu = cbheap->GetCPUDescriptorHandleForHeapStart();
        const auto gpu = cbheap->GetGPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < 4; ++i) {
            D3D12_CONSTANT_BUFFER_VIEW_DESC c{constants[i + 2]->GetGPUVirtualAddress(), 256};
            device->CreateConstantBufferView(&c, {cpu.ptr + i * cstride});
        }
        Com<ID3D12Resource> target;
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = width;
        td.Height = height;
        td.DepthOrArraySize = 1;
        td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES normal{};
        normal.Type = D3D12_HEAP_TYPE_DEFAULT;
        check(device->CreateCommittedResource(&normal, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                              nullptr, IID_PPV_ARGS(&target)));
        Com<ID3D12DescriptorHeap> rtv;
        hd = {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 1;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv)));
        const auto view = rtv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.Get(), nullptr, view);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 total = 0;
        device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, nullptr, nullptr, &total);
        Com<ID3D12Resource> readback;
        D3D12_HEAP_PROPERTIES read{};
        read.Type = D3D12_HEAP_TYPE_READBACK;
        const auto bd = buffer_desc(total);
        check(device->CreateCommittedResource(&read, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&readback)));
        std::array<Com<ID3D12CommandAllocator>, 3> allocators;
        std::array<Com<ID3D12GraphicsCommandList>, 3> lists;
        for (UINT i = 0; i < lists.size(); ++i) {
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])));
            check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].Get(), nullptr,
                                            IID_PPV_ARGS(&lists[i])));
            check(lists[i]->Close());
        }
        Com<ID3D12Fence> fence;
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        const auto close_handle = [](void *value) { CloseHandle(value); };
        std::unique_ptr<void, decltype(close_handle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), close_handle);
        if (!event)
            throw std::runtime_error("fence_event_failed");
        std::ofstream ground(output / "ground-truth.private.json");
        ground << "{\"schemaVersion\":1,\"adapter\":\"WARP\",\"resources\":[";
        for (UINT i = 0; i < 8; ++i) {
            if (i)
                ground << ',';
            ground << "{\"index\":" << i << ",\"nativeHandle\":" << reinterpret_cast<std::uintptr_t>(constants[i].Get())
                   << ",\"bytes\":" << 256 * (i + 1) << '}';
        }
        ground << "]}";
        ground.close();
        if (!ground)
            throw std::runtime_error("ground_truth_write_failed");
        for (UINT frame = 1; frame <= 2; ++frame) {
            for (UINT i = 0; i < lists.size(); ++i) {
                check(allocators[i]->Reset());
                check(lists[i]->Reset(allocators[i].Get(), nullptr));
            }
            const auto draw = [&](ID3D12GraphicsCommandList *list, UINT column, bool table, UINT first) {
                list->SetPipelineState(pipelines[table ? 1 : 0].Get());
                list->SetGraphicsRootSignature(roots[table ? 1 : 0].Get());
                if (table) {
                    ID3D12DescriptorHeap *heaps[] = {cbheap.Get()};
                    list->SetDescriptorHeaps(1, heaps);
                    list->SetGraphicsRootDescriptorTable(0, {gpu.ptr + (first - 2) * cstride});
                    list->SetGraphicsRootDescriptorTable(1, {gpu.ptr + (first - 1) * cstride});
                } else {
                    list->SetGraphicsRootConstantBufferView(0, constants[first]->GetGPUVirtualAddress());
                    list->SetGraphicsRootConstantBufferView(1, constants[first + 1]->GetGPUVirtualAddress());
                }
                D3D12_VIEWPORT vp{static_cast<float>(column * 16), 0, 16, static_cast<float>(height), 0, 1};
                D3D12_RECT sc{static_cast<LONG>(column * 16), 0, static_cast<LONG>((column + 1) * 16), height};
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &sc);
                list->OMSetRenderTargets(1, &view, FALSE, nullptr);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->DrawInstanced(3, 1, 0, 0);
            };
            barrier(lists[0].Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float black[4] = {0, 0, 0, 1};
            lists[0]->ClearRenderTargetView(view, black, 0, nullptr);
            draw(lists[0].Get(), 0, false, 0);
            draw(lists[0].Get(), 1, true, 2);
            draw(lists[0].Get(), 2, true, 4);
            barrier(lists[0].Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION dest{};
            dest.pResource = readback.Get();
            dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dest.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = target.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            lists[2]->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
            check(lists[2]->Close());
            check(lists[0]->Close());
            // Baseline never submits B. The paired control submits B before readback C,
            // proving that this same recorded draw produces yellow when executed.
            barrier(lists[1].Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            draw(lists[1].Get(), 3, false, 6);
            barrier(lists[1].Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            check(lists[1]->Close());
            ID3D12CommandList *baseline[] = {lists[0].Get(), lists[2].Get()};
            ID3D12CommandList *control[] = {lists[0].Get(), lists[1].Get(), lists[2].Get()};
            queue->ExecuteCommandLists(submit_fourth ? 3 : 2, submit_fourth ? control : baseline);
            check(queue->Signal(fence.Get(), frame));
            check(fence->SetEventOnCompletion(frame, event.get()));
            if (WaitForSingleObject(event.get(), 5000) != WAIT_OBJECT_0)
                throw std::runtime_error("owned_gpu_fence_timeout");
            void *bytes = nullptr;
            D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
            check(readback->Map(0, &range, &bytes));
            bool matches = true;
            for (UINT y = 0; y < height; ++y)
                for (UINT x = 0; x < width; ++x) {
                    const auto *pixel = static_cast<const unsigned char *>(bytes) + footprint.Offset +
                                        y * footprint.Footprint.RowPitch + x * 4;
                    const bool yellow = submit_fourth && x >= 48;
                    const unsigned char expected[4] = {
                        static_cast<unsigned char>(x < 16 || yellow ? 255 : 0),
                        static_cast<unsigned char>((x >= 16 && x < 32) || yellow ? 255 : 0),
                        static_cast<unsigned char>(x >= 32 && x < 48 ? 255 : 0), 255};
                    if (std::memcmp(pixel, expected, 4))
                        matches = false;
                }
            D3D12_RANGE no_write{0, 0};
            readback->Unmap(0, &no_write);
            if (!matches)
                throw std::runtime_error("owned_pixel_control_failed");
        }
        std::ofstream result(output / "result.json");
        result << "{\"schemaVersion\":1,\"status\":\"PASS\",\"adapter\":\"WARP\",\"frames\":2,"
                  "\"pixelsVerifiedPerFrame\":2048,\"recordedDrawsPerFrame\":4,\"submittedDrawsPerFrame\":"
               << (submit_fourth ? 4 : 3) << ",\"fourthColumn\":\"" << (submit_fourth ? "yellow" : "black")
               << "\",\"captureTool\":\"NOT_RUN\",\"headset\":\"NOT_RUN\",\"game\":\"NOT_"
                  "RUN\"}";
        result.close();
        if (!result)
            throw std::runtime_error("result_write_failed");
        std::cout << "PASS: WARP two frames, 2048 pixels each; fourth column " << (submit_fourth ? "yellow" : "black")
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
