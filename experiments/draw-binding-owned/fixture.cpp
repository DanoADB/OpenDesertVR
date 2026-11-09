// SPDX-License-Identifier: MIT
// Original WARP geometry/readback control. No game, XR or capture-tool dependency.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "prepared_graphics_replay.hpp"
#include "private_cbv_layout.hpp"
#include "scoped_scalar_patch.hpp"
#include "vertex_constant_stream.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <windows.h>
#include <wrl/client.h>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("owned_draw_graphics_failure");
}
static constexpr UINT width = 64, height = 32;
#if defined(ODV_SCALAR_PATCH_RENDER) || defined(ODV_PRIVATE_TABLE_RENDER) || defined(ODV_PRIVATE_TABLE_SPLIT_RENDER)
static constexpr bool scalar_control = true;
#else
static constexpr bool scalar_control = false;
#endif
#if defined(ODV_PRIVATE_TABLE_RENDER) || defined(ODV_PRIVATE_TABLE_SPLIT_RENDER)
static constexpr bool private_table_control = true;
#else
static constexpr bool private_table_control = false;
#endif
#ifdef ODV_PRIVATE_TABLE_SPLIT_RENDER
static constexpr bool private_split_control = true;
#else
static constexpr bool private_split_control = false;
#endif
#ifdef ODV_VERTEX_STREAM_RENDER
static constexpr bool stream_control = true;
#else
static constexpr bool stream_control = false;
#endif
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
static Com<ID3DBlob> load_owned_blob(const std::filesystem::path &path) {
    const auto size = std::filesystem::file_size(path);
    if (size < 32 || size > 65536)
        throw std::runtime_error("owned_dxil_size_invalid");
    Com<ID3DBlob> blob;
    check(D3DCreateBlob(static_cast<SIZE_T>(size), &blob));
    std::ifstream input(path, std::ios::binary);
    input.read(static_cast<char *>(blob->GetBufferPointer()), static_cast<std::streamsize>(size));
    if (!input || std::memcmp(blob->GetBufferPointer(), "DXBC", 4))
        throw std::runtime_error("owned_dxil_container_invalid");
    return blob;
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
                    D3D12_RESOURCE_STATES after,
                    D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = flags;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &b);
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 2 &&
            (argc != 3 ||
             (std::wstring_view(argv[2]) != L"--submit-fourth" && std::wstring_view(argv[2]) != L"--loop" &&
              std::wstring_view(argv[2]) != L"--loop-dxil" && std::wstring_view(argv[2]) != L"--loop-dxil-budget" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-capture" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-upload" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-upload-empty" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-upload-whole" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-upload-indirect" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-upload-variable" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-upload-transient" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-default-split" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-default-present-gap" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-default-2816" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-enhanced-2816" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-native-pass" &&
              std::wstring_view(argv[2]) != L"--loop-vertex-source" &&
              std::wstring_view(argv[2]) != L"--loop-dxil-world-aspect")))
            throw std::runtime_error("provide_fresh_output_directory");
        const bool budget_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-budget";
        const bool native_pass_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-native-pass";
        const bool vertex_observer_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-vertex-source";
        const bool vertex_source_control = stream_control || vertex_observer_control;
        const bool empty_upload_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-upload-empty";
        const bool whole_upload_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-upload-whole";
        const bool transient_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-upload-transient";
        const bool split_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-default-split";
        const bool aspect_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-world-aspect";
        const bool enhanced_control =
            aspect_control || (argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-enhanced-2816");
        const bool large_default_control = enhanced_control || native_pass_control ||
                                           (argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-default-2816");
        const bool present_gap_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-default-present-gap";
        const bool indirect_control = transient_control || split_control || present_gap_control ||
                                      large_default_control ||
                                      (argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-upload-indirect");
        const bool variable_control = argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-upload-variable";
        const bool upload_control = indirect_control || variable_control || whole_upload_control ||
                                    empty_upload_control ||
                                    (argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-upload");
        const bool capture_control =
            upload_control || (argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil-capture");
        const bool dxil =
            capture_control || budget_control || (argc == 3 && std::wstring_view(argv[2]) == L"--loop-dxil");
        const UINT repeats = budget_control ? 50 : 1;
        const bool loop = dxil || vertex_observer_control || (argc == 3 && std::wstring_view(argv[2]) == L"--loop");
        const bool submit_fourth = argc == 3 && !loop;
        const std::filesystem::path output(argv[1]);
        if (!std::filesystem::is_directory(output) || !std::filesystem::is_empty(output))
            throw std::runtime_error("output_must_be_existing_empty_directory");
        // This baseline must run without a capture DLL beside the executable.
        wchar_t module[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, module, MAX_PATH))
            throw std::runtime_error("module_path_failed");
        const auto executable_dir = std::filesystem::path(module).parent_path();
        for (const auto *name : {L"dxgi.dll", L"d3d12.dll", L"ReShade64.dll"})
            if (!loop && std::filesystem::exists(executable_dir / name))
                throw std::runtime_error("baseline_local_graphics_proxy_present");
        const auto destroy_window = [](HWND value) { DestroyWindow(value); };
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(destroy_window)> window(nullptr, destroy_window);
        if (loop) {
            window.reset(CreateWindowW(L"STATIC", L"OpenDesertVR owned draw observer control", WS_OVERLAPPEDWINDOW, 0,
                                       0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr));
            if (!window)
                throw std::runtime_error("owned_hidden_window_failed");
        }
        Com<ID3D12Debug> debug;
        const bool validation =
            (scalar_control || stream_control) && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        if (validation)
            debug->EnableDebugLayer();
        Com<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        Com<ID3D12InfoQueue> info;
        if (validation)
            check(device.As(&info));
        if (enhanced_control || scalar_control) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
            const auto feature_result =
                device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options));
            if (FAILED(feature_result) && feature_result != E_INVALIDARG && feature_result != E_NOINTERFACE)
                check(feature_result);
            if (FAILED(feature_result) || !options.EnhancedBarriersSupported) {
                if (scalar_control) {
                    std::cout << "NOT_RUN: enhanced barriers unsupported\n";
                    return 77;
                }
                throw std::runtime_error("enhanced_barriers_unsupported");
            }
        }
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC q{};
        check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
        Com<IDXGISwapChain3> swap;
        std::array<Com<ID3D12Resource>, 2> backbuffers;
        if (loop) {
            DXGI_SWAP_CHAIN_DESC1 sd{};
            sd.Width = width;
            sd.Height = height;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            Com<IDXGISwapChain1> initial;
            check(factory->CreateSwapChainForHwnd(queue.Get(), window.get(), &sd, nullptr, nullptr, &initial));
            check(initial.As(&swap));
            check(factory->MakeWindowAssociation(window.get(), DXGI_MWA_NO_ALT_ENTER));
            for (UINT i = 0; i < backbuffers.size(); ++i)
                check(swap->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i])));
        }
        Com<ID3DBlob> vs, ps, errors;
        if (dxil) {
            vs = load_owned_blob(executable_dir / L"owned.dxil.vs.cso");
            ps = load_owned_blob(executable_dir / L"owned.dxil.ps.cso");
        } else {
            check(D3DCompile(shader, sizeof(shader) - 1, "owned_control", nullptr, nullptr, "vs", "vs_5_1",
                             D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &errors));
            check(D3DCompile(shader, sizeof(shader) - 1, "owned_control", nullptr, nullptr, "ps", "ps_5_1",
                             D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &errors));
        }
        write_blob(output / "owned.vs.cso", vs.Get());
        write_blob(output / "owned.ps.cso", ps.Get());
        std::array<Com<ID3D12RootSignature>, 2> roots;
        std::array<Com<ID3D12PipelineState>, 2> pipelines;
        Com<ID3D12RootSignature> private_table_root;
        Com<ID3D12PipelineState> private_table_pipeline;
        for (UINT mode = 0; mode < 2; ++mode) {
            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            D3D12_ROOT_PARAMETER params[2]{};
            for (UINT i = 0; i < 2; ++i) {
                params[i].ShaderVisibility = i == 0 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
                const UINT reg = i == 0 ? 2 : 5, space = i == 0 ? 3 : 7;
                if (mode == 0 && !(private_table_control && i == 0)) {
                    params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    params[i].Descriptor = {reg, space};
                } else {
                    ranges[i] = {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, reg, space, 0};
                    if constexpr (private_split_control) {
                        if (mode == 0 && i == 0) {
                            ranges[i].NumDescriptors = 3;
                            ranges[i].BaseShaderRegister = reg - 1;
                        }
                    }
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
            if constexpr (private_table_control)
                if (mode == 0) {
                    Com<ID3D12VersionedRootSignatureDeserializer> decoder;
                    check(D3D12CreateVersionedRootSignatureDeserializer(
                        serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&decoder)));
                    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *exact = nullptr;
                    check(decoder->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &exact));
                    odv::research::PrivateCbvLayout layout;
                    if (!layout.prepare(exact->Desc_1_1, 0, 2, 3, true) ||
                        layout.private_index() != (private_split_control ? 2u : 0u) ||
                        layout.dword_cost() != (private_split_control ? 5u : 4u))
                        throw std::runtime_error("private_table_derivation_failed");
                    Com<ID3DBlob> lifted;
                    check(D3D12SerializeVersionedRootSignature(layout.description(), &lifted, &errors));
                    check(device->CreateRootSignature(0, lifted->GetBufferPointer(), lifted->GetBufferSize(),
                                                      IID_PPV_ARGS(&private_table_root)));
                }
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
            if constexpr (private_table_control)
                if (mode == 0) {
                    p.pRootSignature = private_table_root.Get();
                    check(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&private_table_pipeline)));
                }
        }
        Com<ID3D12RootSignature> compute_root;
        Com<ID3D12PipelineState> compute_pipeline;
        Com<ID3D12CommandSignature> indirect_signature;
        Com<ID3D12Resource> indirect_arguments;
        if (indirect_control) {
            D3D12_ROOT_SIGNATURE_DESC empty_root{};
            Com<ID3DBlob> root_blob, compute_errors, compute_blob;
            check(D3D12SerializeRootSignature(&empty_root, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &compute_errors));
            check(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                              IID_PPV_ARGS(&compute_root)));
            constexpr char noop[] = "[numthreads(1,1,1)] void main() {}";
            check(D3DCompile(noop, sizeof(noop) - 1, nullptr, nullptr, nullptr, "main", "cs_5_1", 0, 0, &compute_blob,
                             &compute_errors));
            D3D12_COMPUTE_PIPELINE_STATE_DESC cp{};
            cp.pRootSignature = compute_root.Get();
            cp.CS = {compute_blob->GetBufferPointer(), compute_blob->GetBufferSize()};
            check(device->CreateComputePipelineState(&cp, IID_PPV_ARGS(&compute_pipeline)));
            D3D12_INDIRECT_ARGUMENT_DESC argument{};
            argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
            D3D12_COMMAND_SIGNATURE_DESC signature{};
            signature.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
            signature.NumArgumentDescs = 1;
            signature.pArgumentDescs = &argument;
            check(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&indirect_signature)));
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            const auto desc = buffer_desc(256);
            check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                  nullptr, IID_PPV_ARGS(&indirect_arguments)));
            void *mapped = nullptr;
            const D3D12_RANGE empty{};
            check(indirect_arguments->Map(0, &empty, &mapped));
            const D3D12_DRAW_ARGUMENTS args{3, 1, 0, 0};
            std::memcpy(mapped, &args, sizeof(args));
            indirect_arguments->Unmap(0, nullptr);
        }
        const auto constant_bytes = [&](UINT index) -> UINT {
            if (vertex_source_control)
                if (index == 0 || index == 2 || index == 4)
                    return odv::research::VertexConstantStream::byte_count;
            if (large_default_control && (index == 3 || index == 5 || index == 7))
                return 2816;
            return variable_control && index == 5 ? 512u : (variable_control && index == 7 ? 768u : 256u);
        };
        std::array<Com<ID3D12Resource>, 8> constants;
        const float values[8][4] = {{0, 0, 0, 0}, {1, 0, 0, 1}, {0, 0, 0, 0}, {0, 1, 0, 1},
                                    {0, 0, 0, 0}, {0, 0, 1, 1}, {0, 0, 0, 0}, {1, 1, 0, 1}};
        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        for (UINT i = 0; i < constants.size(); ++i) {
            const auto d = buffer_desc(std::max(256ULL * (i + 1), static_cast<UINT64>(constant_bytes(i))));
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
        std::array<Com<ID3D12Resource>, 8> gpu_constants;
        if (scalar_control) {
            D3D12_HEAP_PROPERTIES gpu_heap{};
            gpu_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            const auto desc = constants[0]->GetDesc();
            check(device->CreateCommittedResource(&gpu_heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                  nullptr, IID_PPV_ARGS(&gpu_constants[0])));
        }
        Com<ID3D12Resource> stream_originals;
        if (vertex_source_control) {
            D3D12_HEAP_PROPERTIES gpu_heap{};
            gpu_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            for (UINT i : {0u, 2u, 4u}) {
                const auto desc = constants[i]->GetDesc();
                check(device->CreateCommittedResource(&gpu_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                      IID_PPV_ARGS(&gpu_constants[i])));
            }
            D3D12_HEAP_PROPERTIES read_heap{};
            read_heap.Type = D3D12_HEAP_TYPE_READBACK;
            const auto desc = buffer_desc(3 * odv::research::VertexConstantStream::byte_count);
            check(device->CreateCommittedResource(&read_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&stream_originals)));
        }
        if (upload_control) {
            D3D12_HEAP_PROPERTIES gpu_heap{};
            gpu_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            for (UINT i : {3u, 5u, 7u}) {
                const auto desc = constants[i]->GetDesc();
                check(device->CreateCommittedResource(&gpu_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      enhanced_control ? D3D12_RESOURCE_STATE_COMMON
                                                                       : D3D12_RESOURCE_STATE_COPY_DEST,
                                                      nullptr, IID_PPV_ARGS(&gpu_constants[i])));
            }
        }
        const auto bound_constant = [&](UINT i) {
            return gpu_constants[i] ? gpu_constants[i].Get() : constants[i].Get();
        };
        const auto constant_ready = [&](ID3D12GraphicsCommandList *list, UINT index) {
            if (enhanced_control) {
                Com<ID3D12GraphicsCommandList7> native;
                check(list->QueryInterface(IID_PPV_ARGS(&native)));
                D3D12_BUFFER_BARRIER b{};
                b.SyncBefore = D3D12_BARRIER_SYNC_COPY;
                b.SyncAfter = D3D12_BARRIER_SYNC_ALL;
                b.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
                b.AccessAfter = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
                b.pResource = gpu_constants[index].Get();
                b.Size = UINT64_MAX;
                D3D12_BARRIER_GROUP group{};
                group.Type = D3D12_BARRIER_TYPE_BUFFER;
                group.NumBarriers = 1;
                group.pBufferBarriers = &b;
                native->Barrier(1, &group);
                return;
            }
            if (split_control)
                barrier(list, gpu_constants[index].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
            barrier(list, gpu_constants[index].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                    split_control ? D3D12_RESOURCE_BARRIER_FLAG_END_ONLY : D3D12_RESOURCE_BARRIER_FLAG_NONE);
        };
        const auto constant_prepare = [&](ID3D12GraphicsCommandList *list, UINT index) {
            if (!enhanced_control)
                return;
            Com<ID3D12GraphicsCommandList7> native;
            check(list->QueryInterface(IID_PPV_ARGS(&native)));
            D3D12_BUFFER_BARRIER b{};
            b.SyncBefore = D3D12_BARRIER_SYNC_ALL;
            b.SyncAfter = D3D12_BARRIER_SYNC_COPY;
            b.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
            b.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
            b.pResource = gpu_constants[index].Get();
            b.Size = UINT64_MAX;
            D3D12_BARRIER_GROUP group{};
            group.Type = D3D12_BARRIER_TYPE_BUFFER;
            group.NumBarriers = 1;
            group.pBufferBarriers = &b;
            native->Barrier(1, &group);
        };
        Com<ID3D12DescriptorHeap> cbheap;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = private_table_control ? (private_split_control ? 10 : 6) : (capture_control ? 6 : 4);
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&cbheap)));
        const auto cstride = device->GetDescriptorHandleIncrementSize(hd.Type);
        const auto cpu = cbheap->GetCPUDescriptorHandleForHeapStart();
        const auto gpu = cbheap->GetGPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < hd.NumDescriptors; ++i) {
            const UINT source_index =
                private_table_control && i >= 4 ? (i < (private_split_control ? 7u : 5u) ? 0 : 6) : i + 2;
            D3D12_CONSTANT_BUFFER_VIEW_DESC c{bound_constant(source_index)->GetGPUVirtualAddress(),
                                              constant_bytes(source_index)};
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
        hd.NumDescriptors = loop ? (aspect_control ? 4 : 3) : 1;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv)));
        const auto view = rtv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.Get(), nullptr, view);
        const auto rstride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        Com<ID3D12Resource> square_target;
        if (aspect_control) {
            auto square_desc = td;
            square_desc.Width = square_desc.Height = 32;
            check(device->CreateCommittedResource(&normal, D3D12_HEAP_FLAG_NONE, &square_desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                  IID_PPV_ARGS(&square_target)));
            device->CreateRenderTargetView(square_target.Get(), nullptr, {view.ptr + 3 * rstride});
        }
        if (loop)
            for (UINT i = 0; i < backbuffers.size(); ++i)
                device->CreateRenderTargetView(backbuffers[i].Get(), nullptr, {view.ptr + (i + 1) * rstride});
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 total = 0;
        device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, nullptr, nullptr, &total);
        Com<ID3D12Resource> readback;
        D3D12_HEAP_PROPERTIES read{};
        read.Type = D3D12_HEAP_TYPE_READBACK;
        const auto bd = buffer_desc(total);
        check(device->CreateCommittedResource(&read, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&readback)));
        Com<ID3D12Resource> perturbed_readback, restored_input;
        if (scalar_control) {
            check(device->CreateCommittedResource(&read, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr, IID_PPV_ARGS(&perturbed_readback)));
            const auto input_desc = buffer_desc(256);
            check(device->CreateCommittedResource(&read, D3D12_HEAP_FLAG_NONE, &input_desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&restored_input)));
        }
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
        const auto write_ground = [&](const wchar_t *name) {
            std::ofstream ground(output / name);
            ground << "{\"schemaVersion\":1,\"adapter\":\"WARP\",\"resources\":[";
            for (UINT i = 0; i < 8; ++i) {
                if (i)
                    ground << ',';
                ground << "{\"index\":" << i
                       << ",\"nativeHandle\":" << reinterpret_cast<std::uintptr_t>(bound_constant(i))
                       << ",\"bytes\":" << bound_constant(i)->GetDesc().Width << '}';
            }
            ground << "]}";
            ground.close();
            if (!ground)
                throw std::runtime_error("ground_truth_write_failed");
        };
        write_ground(L"ground-truth.private.json");
        bool rolled_over = false;
        UINT completed = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (UINT frame = 1; loop || frame <= 2; ++frame) {
            odv::research::ScopedScalarPatch scalar_patch;
            std::array<odv::research::VertexConstantStream, 3> stream_samples;
            Com<ID3D12GraphicsCommandList7> scalar_commands;
            if (loop && (std::filesystem::exists(output.parent_path() / L"stop.flag") ||
                         std::chrono::steady_clock::now() >= deadline))
                break;
            if (present_gap_control && frame == 2) {
                {
                    std::ofstream ready(output / L"present-gap.ready");
                    ready << "waiting before two empty presentation intervals\n";
                    if (!ready)
                        throw std::runtime_error("present_gap_readiness_failed");
                }
                // The controller queues exactly one snapshot before releasing
                // this gate. No app draws occur between the next two Presents.
                while (!std::filesystem::exists(output.parent_path() / L"present-gap.go") &&
                       !std::filesystem::exists(output.parent_path() / L"stop.flag") &&
                       std::chrono::steady_clock::now() < deadline)
                    Sleep(2);
                if (!std::filesystem::exists(output.parent_path() / L"present-gap.go"))
                    break;
                check(swap->Present(1, 0));
                check(swap->Present(1, 0));
            }
            MSG message{};
            while (loop && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (transient_control) {
                // The previous frame's fence completed before reaching here.
                // Drop the app's reference to each previous upload allocation;
                // the observer may now own its final reference when read later.
                // Bound DEFAULT targets and authored pixels remain unchanged.
                for (UINT i : {3u, 5u, 7u}) {
                    const auto desc = constants[i]->GetDesc();
                    Com<ID3D12Resource> replacement;
                    check(device->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                          IID_PPV_ARGS(&replacement)));
                    void *mapped = nullptr;
                    const D3D12_RANGE empty{};
                    check(replacement->Map(0, &empty, &mapped));
                    std::memset(mapped, 0, static_cast<size_t>(desc.Width));
                    std::memcpy(mapped, values[i], sizeof(values[i]));
                    constants[i] = std::move(replacement);
                }
            }
            for (UINT i = 0; i < lists.size(); ++i) {
                check(allocators[i]->Reset());
                check(lists[i]->Reset(allocators[i].Get(), nullptr));
            }
            const bool rollover =
                transient_control && !rolled_over && std::filesystem::exists(output.parent_path() / L"rollover.flag");
            if (rollover) {
                // Completed GPU use and retired command recordings: simulate
                // scene resource replacement while old capture tickets exist.
                D3D12_HEAP_PROPERTIES gpu_heap{};
                gpu_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
                for (UINT i : {3u, 5u, 7u}) {
                    const auto desc = gpu_constants[i]->GetDesc();
                    Com<ID3D12Resource> replacement;
                    check(device->CreateCommittedResource(&gpu_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                          IID_PPV_ARGS(&replacement)));
                    gpu_constants[i] = std::move(replacement);
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{bound_constant(i)->GetGPUVirtualAddress(), constant_bytes(i)};
                    device->CreateConstantBufferView(&cbv, {cpu.ptr + (i - 2) * cstride});
                }
                write_ground(L"rollover-ground-truth.private.json");
                rolled_over = true;
            }
            if (indirect_control) {
                // Leave a cached compute pipeline before binding graphics PSOs.
                // The following ExecuteIndirect operations are genuine draws.
                lists[0]->SetPipelineState(compute_pipeline.Get());
                lists[0]->SetComputeRootSignature(compute_root.Get());
                lists[0]->Dispatch(1, 1, 1);
            }
            D3D12_GPU_VIRTUAL_ADDRESS private_vertex_address = 0;
            D3D12_CPU_DESCRIPTOR_HANDLE draw_view = view;
            const auto bind_draw = [&](ID3D12GraphicsCommandList *list, UINT column, bool table, UINT first) noexcept {
                const bool lifted = private_table_control && !table && private_vertex_address != 0;
                list->SetPipelineState(lifted ? private_table_pipeline.Get() : pipelines[table ? 1 : 0].Get());
                list->SetGraphicsRootSignature(lifted ? private_table_root.Get() : roots[table ? 1 : 0].Get());
                if (table) {
                    ID3D12DescriptorHeap *heaps[] = {cbheap.Get()};
                    list->SetDescriptorHeaps(1, heaps);
                    list->SetGraphicsRootDescriptorTable(0, {gpu.ptr + (first - 2) * cstride});
                    list->SetGraphicsRootDescriptorTable(1, {gpu.ptr + (first - 1) * cstride});
                } else {
                    if constexpr (private_table_control) {
                        if constexpr (private_split_control) {
                            ID3D12DescriptorHeap *heaps[] = {cbheap.Get()};
                            list->SetDescriptorHeaps(1, heaps);
                            list->SetGraphicsRootDescriptorTable(0, {gpu.ptr + (first == 0 ? 4 : 7) * cstride});
                            if (lifted)
                                list->SetGraphicsRootConstantBufferView(2, private_vertex_address);
                        } else if (!lifted) {
                            ID3D12DescriptorHeap *heaps[] = {cbheap.Get()};
                            list->SetDescriptorHeaps(1, heaps);
                            list->SetGraphicsRootDescriptorTable(0, {gpu.ptr + (first == 0 ? 4 : 5) * cstride});
                        } else {
                            list->SetGraphicsRootConstantBufferView(0, private_vertex_address);
                        }
                    } else {
                        list->SetGraphicsRootConstantBufferView(0, private_vertex_address && first == 0
                                                                       ? private_vertex_address
                                                                       : bound_constant(first)->GetGPUVirtualAddress());
                    }
                    list->SetGraphicsRootConstantBufferView(1, constants[first + 1]->GetGPUVirtualAddress());
                }
                D3D12_VIEWPORT vp{static_cast<float>(column * 16), 0, 16, static_cast<float>(height), 0, 1};
                D3D12_RECT sc{static_cast<LONG>(column * 16), 0, static_cast<LONG>((column + 1) * 16), height};
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &sc);
                list->OMSetRenderTargets(1, &draw_view, FALSE, nullptr);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            };
            const auto draw = [&](ID3D12GraphicsCommandList *list, UINT column, bool table, UINT first) {
                bind_draw(list, column, table, first);
                if constexpr (stream_control) {
                    if (column < 3) {
                        const odv::research::VertexConstantStream::Context context{true, true, true, true,
                                                                                   true, true, true};
                        odv::research::PreparedGraphicsReplay replay;
                        replay.root = roots[table ? 1 : 0].Get();
                        replay.pipeline = pipelines[table ? 1 : 0].Get();
                        replay.root_count = 2;
                        for (UINT index = 0; index < 2; ++index) {
                            replay.arguments[index].kind = table
                                                               ? odv::research::PreparedGraphicsReplay::RootKind::table
                                                               : odv::research::PreparedGraphicsReplay::RootKind::cbv;
                            replay.arguments[index].address =
                                table ? gpu.ptr + (first - 2 + index) * cstride
                                      : bound_constant(first + index)->GetGPUVirtualAddress();
                        }
                        replay.target_count = 1;
                        replay.targets[0] = draw_view;
                        replay.targets_known = replay.stream_outputs_known = true;
                        replay.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                        if (!replay.ready())
                            throw std::runtime_error("prepared_replay_incomplete");
                        const auto restore = [&]() noexcept { replay.apply(list); };
                        if (!stream_samples[column].record(device.Get(), list, bound_constant(first), 0, context,
                                                           restore))
                            throw std::runtime_error("vertex_stream_render_record_failed");
                    }
                }
                // No application-state rebinding after the reader's restore.
                Com<ID3D12GraphicsCommandList4> render_pass;
                if (native_pass_control) {
                    check(list->QueryInterface(IID_PPV_ARGS(&render_pass)));
                    D3D12_RENDER_PASS_RENDER_TARGET_DESC target_pass{};
                    target_pass.cpuDescriptor = draw_view;
                    target_pass.BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                    target_pass.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
                    render_pass->BeginRenderPass(1, &target_pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
                }
                if (indirect_control)
                    list->ExecuteIndirect(indirect_signature.Get(), 1, indirect_arguments.Get(), 0, nullptr, 0);
                else
                    list->DrawInstanced(3, 1, 0, 0);
                if (native_pass_control)
                    render_pass->EndRenderPass();
            };
            barrier(lists[0].Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float black[4] = {0, 0, 0, 1};
            lists[0]->ClearRenderTargetView(view, black, 0, nullptr);
            if (vertex_source_control)
                for (UINT i : {0u, 2u, 4u}) {
                    barrier(lists[0].Get(), gpu_constants[i].Get(), D3D12_RESOURCE_STATE_COMMON,
                            D3D12_RESOURCE_STATE_COPY_DEST);
                    lists[0]->CopyBufferRegion(gpu_constants[i].Get(), 0, constants[i].Get(), 0, constant_bytes(i));
                    barrier(lists[0].Get(), gpu_constants[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                }
            if (upload_control)
                for (UINT i : {3u, 5u}) {
                    constant_prepare(lists[0].Get(), i);
                    if (whole_upload_control)
                        lists[0]->CopyResource(gpu_constants[i].Get(), constants[i].Get());
                    else
                        lists[0]->CopyBufferRegion(gpu_constants[i].Get(), 0, constants[i].Get(), 0, constant_bytes(i));
                    constant_ready(lists[0].Get(), i);
                }
            if (aspect_control) {
                const D3D12_CPU_DESCRIPTOR_HANDLE square_view{view.ptr + 3 * rstride};
                lists[0]->OMSetRenderTargets(1, &square_view, FALSE, nullptr);
                lists[0]->ClearRenderTargetView(square_view, black, 0, nullptr);
                draw_view = square_view;
                draw(lists[0].Get(), 0, false, 0); // Extra submitted square pass: must be filtered.
                draw_view = view;
                lists[0]->OMSetRenderTargets(1, &view, FALSE, nullptr);
            }
            if (scalar_control) {
                check(lists[0].As(&scalar_commands));
                D3D12_BUFFER_BARRIER b{};
                b.pResource = gpu_constants[0].Get();
                b.Size = UINT64_MAX;
                D3D12_BARRIER_GROUP group{};
                group.Type = D3D12_BARRIER_TYPE_BUFFER;
                group.NumBarriers = 1;
                group.pBufferBarriers = &b;
                b.SyncBefore = D3D12_BARRIER_SYNC_ALL;
                b.SyncAfter = D3D12_BARRIER_SYNC_COPY;
                b.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
                b.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
                scalar_commands->Barrier(1, &group);
                lists[0]->CopyBufferRegion(gpu_constants[0].Get(), 0, constants[0].Get(), 0, 256);
                b.SyncBefore = D3D12_BARRIER_SYNC_COPY;
                b.SyncAfter = D3D12_BARRIER_SYNC_ALL;
                b.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
                b.AccessAfter = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
                scalar_commands->Barrier(1, &group);
                odv::research::KnownBufferStates states;
                states.observe_enhanced(1, &group);
                if (!scalar_patch.begin_private(device.Get(), scalar_commands.Get(), gpu_constants[0].Get(), 0, 256, 0,
                                                2.0f, states))
                    throw std::runtime_error("scalar_patch_begin_failed");
                private_vertex_address = scalar_patch.private_gpu_address();
                if (!private_vertex_address || private_vertex_address == gpu_constants[0]->GetGPUVirtualAddress())
                    throw std::runtime_error("private_vertex_binding_invalid");
                draw(lists[0].Get(), 0, false, 0); // Shift triangle fully outside its viewport.
                if (!scalar_patch.end(scalar_commands.Get()))
                    throw std::runtime_error("scalar_patch_restore_failed");
                private_vertex_address = 0;
                barrier(lists[0].Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION changed{};
                changed.pResource = perturbed_readback.Get();
                changed.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                changed.PlacedFootprint = footprint;
                D3D12_TEXTURE_COPY_LOCATION rendered{};
                rendered.pResource = target.Get();
                rendered.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                lists[0]->CopyTextureRegion(&changed, 0, 0, 0, &rendered, nullptr);
                barrier(lists[0].Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                        D3D12_RESOURCE_STATE_RENDER_TARGET);
                b.SyncBefore = D3D12_BARRIER_SYNC_ALL;
                b.SyncAfter = D3D12_BARRIER_SYNC_COPY;
                b.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
                b.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
                scalar_commands->Barrier(1, &group);
                lists[0]->CopyBufferRegion(restored_input.Get(), 0, gpu_constants[0].Get(), 0, 256);
                b.SyncBefore = D3D12_BARRIER_SYNC_COPY;
                b.SyncAfter = D3D12_BARRIER_SYNC_ALL;
                b.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
                b.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
                scalar_commands->Barrier(1, &group);
            }
            for (UINT repeat = 0; repeat < repeats; ++repeat) {
                draw(lists[0].Get(), 0, false, 0);
                draw(lists[0].Get(), 1, true, 2);
                draw(lists[0].Get(), 2, true, 4);
            }
            if (upload_control && !enhanced_control)
                for (UINT i : {3u, 5u})
                    barrier(lists[0].Get(), gpu_constants[i].Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                            D3D12_RESOURCE_STATE_COPY_DEST);
            if (vertex_source_control)
                for (UINT i : {0u, 2u, 4u}) {
                    barrier(lists[0].Get(), gpu_constants[i].Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                            D3D12_RESOURCE_STATE_COPY_SOURCE);
                    lists[0]->CopyBufferRegion(stream_originals.Get(), (i / 2) * constant_bytes(i),
                                               gpu_constants[i].Get(), 0, constant_bytes(i));
                    barrier(lists[0].Get(), gpu_constants[i].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                            D3D12_RESOURCE_STATE_COMMON);
                }
            barrier(lists[0].Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION dest{};
            dest.pResource = readback.Get();
            dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dest.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = target.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            lists[2]->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
            if (loop) {
                const auto index = swap->GetCurrentBackBufferIndex();
                barrier(lists[2].Get(), backbuffers[index].Get(), D3D12_RESOURCE_STATE_PRESENT,
                        D3D12_RESOURCE_STATE_RENDER_TARGET);
                lists[2]->ClearRenderTargetView({view.ptr + (index + 1) * rstride}, black, 0, nullptr);
                barrier(lists[2].Get(), backbuffers[index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_PRESENT);
            }
            check(lists[2]->Close());
            check(lists[0]->Close());
            // Baseline never submits B. The paired control submits B before readback C,
            // proving that this same recorded draw produces yellow when executed.
            barrier(lists[1].Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            if (upload_control) {
                constant_prepare(lists[1].Get(), 7);
                if (whole_upload_control)
                    lists[1]->CopyResource(gpu_constants[7].Get(), constants[7].Get());
                else
                    lists[1]->CopyBufferRegion(gpu_constants[7].Get(), 0, constants[7].Get(), 0, constant_bytes(7));
                constant_ready(lists[1].Get(), 7);
            }
            draw(lists[1].Get(), 3, capture_control, 6);
            if (upload_control && !enhanced_control)
                barrier(lists[1].Get(), gpu_constants[7].Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                        D3D12_RESOURCE_STATE_COPY_DEST);
            barrier(lists[1].Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            check(lists[1]->Close());
            if (empty_upload_control) {
                // Retire B's upload/draw, then submit the EMPTY replacement.
                // Its old copy must not gain data merely from that execution.
                check(lists[1]->Reset(allocators[1].Get(), nullptr));
                check(lists[1]->Close());
            }
            ID3D12CommandList *baseline[] = {lists[0].Get(), lists[2].Get()};
            ID3D12CommandList *control[] = {lists[0].Get(), lists[1].Get(), lists[2].Get()};
            if constexpr (scalar_control)
                if (!scalar_patch.before_execute())
                    throw std::runtime_error("scalar_patch_execution_rejected");
            if (stream_control)
                for (auto &sample : stream_samples)
                    if (!sample.read().bytes.empty() || !sample.before_execute())
                        throw std::runtime_error("stream_render_unsubmitted_control_failed");
            queue->ExecuteCommandLists(submit_fourth || empty_upload_control ? 3 : 2,
                                       submit_fourth || empty_upload_control ? control : baseline);
            check(queue->Signal(fence.Get(), frame));
            if constexpr (scalar_control)
                if (!scalar_patch.submitted_after_execute(fence.Get(), frame))
                    throw std::runtime_error("scalar_patch_fence_rejected");
            if (stream_control)
                for (auto &sample : stream_samples)
                    if (!sample.submitted_after_execute(fence.Get(), frame))
                        throw std::runtime_error("stream_render_fence_rejected");
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
            if (vertex_source_control) {
                const std::array<std::byte, odv::research::VertexConstantStream::byte_count> expected{};
                if (stream_control)
                    for (auto &sample : stream_samples) {
                        const auto actual = sample.read();
                        if (std::strcmp(actual.state, "READY") || actual.bytes.size() != expected.size() ||
                            std::memcmp(actual.bytes.data(), expected.data(), expected.size()))
                            throw std::runtime_error("stream_render_sample_failed");
                    }
                const D3D12_RANGE source_range{0, expected.size() * 3};
                check(stream_originals->Map(0, &source_range, &bytes));
                bool exact = true;
                for (UINT i = 0; i < 3; ++i)
                    exact &= std::memcmp(static_cast<const std::byte *>(bytes) + i * expected.size(), expected.data(),
                                         expected.size()) == 0;
                stream_originals->Unmap(0, &no_write);
                if (!exact)
                    throw std::runtime_error("stream_render_original_source_modified");
                if (stream_control) {
                    check(allocators[0]->Reset());
                    check(lists[0]->Reset(allocators[0].Get(), nullptr));
                    check(lists[0]->Close());
                    for (auto &sample : stream_samples)
                        if (!sample.release_retired())
                            throw std::runtime_error("stream_render_retirement_failed");
                    odv::research::DeferredResourceRelease::drain();
                }
            }
            if (scalar_control) {
                check(perturbed_readback->Map(0, &range, &bytes));
                bool empty_geometry = true;
                const unsigned char black_pixel[4] = {0, 0, 0, 255};
                for (UINT y = 0; y < height; ++y)
                    for (UINT x = 0; x < width; ++x) {
                        const auto *pixel = static_cast<const unsigned char *>(bytes) + footprint.Offset +
                                            y * footprint.Footprint.RowPitch + x * 4;
                        if (std::memcmp(pixel, black_pixel, 4))
                            empty_geometry = false;
                    }
                perturbed_readback->Unmap(0, &no_write);
                const D3D12_RANGE input_range{0, 256};
                check(restored_input->Map(0, &input_range, &bytes));
                const std::array<std::byte, 256> original{};
                const bool exact_restoration = std::memcmp(bytes, original.data(), original.size()) == 0;
                restored_input->Unmap(0, &no_write);
                if (!empty_geometry || !exact_restoration)
                    throw std::runtime_error("scalar_geometry_or_restoration_failed");
                check(allocators[0]->Reset());
                check(lists[0]->Reset(allocators[0].Get(), nullptr)); // Retires actual recording.
                check(lists[0]->Close());
                if (!scalar_patch.release_retired())
                    throw std::runtime_error("scalar_patch_retirement_failed");
                odv::research::DeferredResourceRelease::drain();
            }
            if (loop)
                check(swap->Present(1, 0));
            if (rollover) {
                std::ofstream ready(output / L"rollover.ready");
                ready << "replacement DEFAULT resources and GPU pixels verified\n";
                if (!ready)
                    throw std::runtime_error("owned_rollover_write_failed");
            }
            ++completed;
            if (loop && completed == 1) {
                std::ofstream ready(output / L"ready.flag");
                ready << "owned GPU pixels verified\n";
                if (!ready)
                    throw std::runtime_error("owned_readiness_write_failed");
            }
        }
        if (!completed)
            throw std::runtime_error("owned_fixture_rendered_no_frames");
        if (info)
            for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T size = 0;
                check(info->GetMessage(i, nullptr, &size));
                auto storage = std::make_unique<std::byte[]>(size);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.get());
                check(info->GetMessage(i, message, &size));
                if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                    message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
                    throw std::runtime_error("scalar_render_debug_validation_failed");
            }
        std::ofstream result(output / "result.json");
        result << "{\"schemaVersion\":1,\"status\":\"PASS\",\"adapter\":\"WARP\",\"frames\":" << completed << ','
               << "\"pixelsVerifiedPerFrame\":2048,\"recordedDrawsPerFrame\":"
               << repeats * 3 + 1 + (scalar_control ? 1 : 0) + (aspect_control ? 1 : 0) + (stream_control ? 3 : 0)
               << ",\"submittedDrawsPerFrame\":"
               << repeats * 3 + (submit_fourth ? 1 : 0) + (scalar_control ? 1 : 0) + (aspect_control ? 1 : 0) +
                      (stream_control ? 3 : 0)
               << ",\"fourthColumn\":\"" << (submit_fourth ? "yellow" : "black") << "\",\"captureTool\":\""
               << (loop ? "EXTERNAL_OBSERVER_POSSIBLE" : "NOT_RUN")
               << "\",\"headset\":\"NOT_RUN\",\"game\":\"NOT_"
                  "RUN\"}";
        if (scalar_control)
            std::cout
                << "PASS: private-copy geometry 2048 black pixels, original geometry 2048 RGB/black pixels, exact "
                   "256 input bytes; debug="
                << (validation ? "ENABLED" : "UNAVAILABLE") << '\n';
        if (private_table_control)
            std::cout
                << "PASS: exact derived descriptor-table layout, private root-CBV/PSO and original table restore; "
                   "same shader bytes; no original descriptor edits\n";
        if (private_split_control)
            std::cout << "PASS: interior b2 removed from b1-b3 table, private root appended at index 2; "
                         "original table and shaders restored\n";
        if (stream_control)
            std::cout << "PASS: stream read restores root-CBV/table PSO, roots, targets, topology and unbound SO; "
                         "next draws execute without rebinding; 2048 RGB/black pixels and three 2816-byte original "
                         "sources exact; debug="
                      << (validation ? "ENABLED" : "UNAVAILABLE") << '\n';
        result.close();
        if (!result)
            throw std::runtime_error("result_write_failed");
        std::cout << "PASS: WARP " << completed << " frames, 2048 pixels each; fourth column "
                  << (submit_fourth ? "yellow" : "black") << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
