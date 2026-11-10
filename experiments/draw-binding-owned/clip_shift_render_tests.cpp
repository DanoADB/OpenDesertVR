// SPDX-License-Identifier: MIT
// Owned native GPU control for the measured 40-byte indirect format. No game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../renodx-owned/native_clip_indirect.hpp"
#include "../renodx-owned/native_vertex_pipeline_payload.hpp"
#include "clip_shift_dxil.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifdef ODV_TEST_AGILITY_SDK
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = ODV_TEST_AGILITY_SDK;
__declspec(dllexport) extern const char *D3D12SDKPath = ".\\D3D12Test\\";
}
#endif

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static const char *stage = "initialization";
static void require(bool condition) {
    if (!condition)
        throw std::runtime_error("owned_clip_shift_control_failed");
}
static void checked(HRESULT result) {
    if (FAILED(result))
        std::cerr << "native_hresult=0x" << std::hex << static_cast<unsigned long>(result) << std::dec << '\n';
    require(SUCCEEDED(result));
}
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
    if (!equal) {
        const auto *a = static_cast<const unsigned char *>(mapped);
        const auto *b = static_cast<const unsigned char *>(original);
        for (SIZE_T i = 0; i < bytes; ++i)
            if (a[i] != b[i]) {
                std::cerr << "preservation_bytes=" << bytes << " mismatch_offset=" << i
                          << " expected_byte=" << UINT(b[i]) << " actual_byte=" << UINT(a[i]) << '\n';
                break;
            }
    }
    const D3D12_RANGE no_write{};
    resource->Unmap(0, &no_write);
    require(equal);
}
static Com<ID3D12Resource> gpu_buffer(ID3D12Device *device, SIZE_T bytes) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto desc = buffer(bytes);
    Com<ID3D12Resource> result;
    checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&result)));
    return result;
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
struct TestModule {
    HMODULE value = nullptr;
    ~TestModule() {
        if (value)
            FreeLibrary(value);
    }
};
static void report_warp(HMODULE expected) {
    const auto loaded = GetModuleHandleW(L"d3d10warp.dll");
    require(loaded && (!expected || loaded == expected));
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(loaded, path.data(), static_cast<DWORD>(path.size()));
    require(length && length < path.size());
#ifdef ODV_TEST_WARP_DLL
    if (expected)
        require(
            std::filesystem::equivalent(std::filesystem::path(path.data()), std::filesystem::path(ODV_TEST_WARP_DLL)));
#endif
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(path.data(), &ignored);
    require(bytes > 0 && bytes <= 1024 * 1024);
    std::vector<std::byte> storage(bytes);
    checked(GetFileVersionInfoW(path.data(), 0, bytes, storage.data()) ? S_OK : E_FAIL);
    VS_FIXEDFILEINFO *version = nullptr;
    UINT size = 0;
    require(VerQueryValueW(storage.data(), L"\\", reinterpret_cast<void **>(&version), &size) &&
            size >= sizeof(*version));
    std::cout << "warp_kind=" << (expected ? "PINNED_TEST_ONLY" : "SYSTEM")
              << " version=" << HIWORD(version->dwFileVersionMS) << '.' << LOWORD(version->dwFileVersionMS) << '.'
              << HIWORD(version->dwFileVersionLS) << '.' << LOWORD(version->dwFileVersionLS) << '\n';
}

// Native D3D12 stream records require pointer-aligned trailing padding.
#pragma warning(push)
#pragma warning(disable : 4324)
template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Kind, class T> struct alignas(void *) StreamPart {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE kind = Kind;
    T data{};
};
struct OwnedStream {
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature *> root;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> mask;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> raster;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depth;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> formats;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depthformat;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> samples;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE> strip;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT> node;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS> flags;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC> so;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> input;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, D3D12_CACHED_PIPELINE_STATE> cache;
    explicit OwnedStream(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &d) {
        root.data = d.pRootSignature;
        vs.data = d.VS;
        ps.data = d.PS;
        blend.data = d.BlendState;
        mask.data = d.SampleMask;
        raster.data = d.RasterizerState;
        depth.data = d.DepthStencilState;
        topology.data = d.PrimitiveTopologyType;
        depthformat.data = d.DSVFormat;
        samples.data = d.SampleDesc;
        strip.data = d.IBStripCutValue;
        node.data = d.NodeMask;
        flags.data = d.Flags;
        so.data = d.StreamOutput;
        input.data = d.InputLayout;
        cache.data = d.CachedPSO;
        formats.data.NumRenderTargets = d.NumRenderTargets;
        std::memcpy(formats.data.RTFormats, d.RTVFormats, sizeof(d.RTVFormats));
    }
};
#pragma warning(pop)
static_assert(alignof(OwnedStream) == alignof(void *));
static std::vector<std::uint8_t> read_owned(const std::filesystem::path &path) {
    require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) <= 1024 * 1024);
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
int wmain(int argc, wchar_t **argv) {
    TestModule warp;
    Com<ID3D12InfoQueue> info;
    try {
        require(argc >= 4 && argc <= 6);
        bool use_stream = false, retained_native = false;
        for (int i = 4; i < argc; ++i) {
            const std::wstring_view argument(argv[i]);
            require(argument == L"--stream" || argument == L"--retained-native");
            if (argument == L"--stream")
                use_stream = true;
            if (argument == L"--retained-native")
                retained_native = true;
        }
#ifdef ODV_TEST_WARP_DLL
        warp.value =
            LoadLibraryExW(ODV_TEST_WARP_DLL, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(warp.value != nullptr);
#endif
        odv::research::ClipShiftCompiler compiler{std::filesystem::path(argv[1])};
        const auto vs = read_owned(argv[2]), ps = read_owned(argv[3]);
        stage = "dxil_assembly_and_validation";
        std::array<std::vector<std::uint8_t>, 4> shaders{vs, compiler.compile(vs, odv::research::ClipShift::identity),
                                                         compiler.compile(vs, odv::research::ClipShift::left),
                                                         compiler.compile(vs, odv::research::ClipShift::right)};
        Com<ID3D12Debug> debug;
        const HRESULT debug_status = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        const bool validation = SUCCEEDED(debug_status);
        if (!validation)
            std::cout << "debug_interface_hresult=0x" << std::hex << static_cast<unsigned long>(debug_status)
                      << std::dec << '\n';
        if (validation)
            debug->EnableDebugLayer();
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        checked(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        report_warp(warp.value);
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_6};
        checked(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)));
        std::cout << "shader_model=0x" << std::hex << model.HighestShaderModel << std::dec;
#ifdef ODV_TEST_AGILITY_SDK
        const auto core = GetModuleHandleW(L"D3D12Core.dll");
        require(core != nullptr);
        const auto sdk = reinterpret_cast<const UINT *>(GetProcAddress(core, "D3D12SDKVersion"));
        require(sdk && *sdk >= ODV_TEST_AGILITY_SDK);
        std::array<wchar_t, 32768> core_path{};
        require(GetModuleFileNameW(core, core_path.data(), static_cast<DWORD>(core_path.size())) > 0);
        const bool bundled = std::filesystem::path(core_path.data()).parent_path().filename() == L"D3D12Test";
        std::cout << " agility_requested=" << ODV_TEST_AGILITY_SDK << " core_sdk=" << *sdk
                  << " core_source=" << (bundled ? "ISOLATED_TEST" : "OS_NEWER");
#endif
        std::cout << '\n';
        require(model.HighestShaderModel >= D3D_SHADER_MODEL_6_6);
        if (validation)
            checked(device.As(&info));
        stage = "original_root_and_private_pso";
        D3D12_ROOT_PARAMETER1 parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants = {0, 99, 1};
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[1].Descriptor = {18, 35, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        root_desc.Desc_1_1 = {2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        Com<ID3DBlob> root_blob, errors;
        checked(D3D12SerializeVersionedRootSignature(&root_desc, &root_blob, &errors));
        Com<ID3D12RootSignature> root;
        checked(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root)));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.VS = {vs.data(), vs.size()};
        pso_desc.PS = {ps.data(), ps.size()};
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

        pso_desc.pRootSignature = root.Get();
        pso_desc.NodeMask = 1;
        pso_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
        std::array<Com<ID3D12PipelineState>, 4> pipelines;
        if (use_stream) {
            OwnedStream original_stream{pso_desc};
            const D3D12_PIPELINE_STATE_STREAM_DESC original_desc{sizeof(original_stream), &original_stream};
            Com<ID3D12Device2> device2;
            checked(device.As(&device2));
            checked(device2->CreatePipelineState(&original_desc, IID_PPV_ARGS(&pipelines[0])));
        } else {
            checked(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipelines[0])));
        }
        stage = "native_source_bounds_and_ownership";
        {
            auto reject = [](const D3D12_GRAPHICS_PIPELINE_STATE_DESC &d) {
                odv::research::NativeVertexPipelineSource invalid;
                require(!invalid.prepare(d) && !invalid.root() && !invalid.vertex().BytecodeLength);
                require(!invalid.prepare(d));
            };
            auto invalid = pso_desc;
            invalid.GS = invalid.VS;
            reject(invalid);
            invalid = pso_desc;
            invalid.StreamOutput.NumEntries = 1;
            reject(invalid);
            invalid = pso_desc;
            invalid.InputLayout.NumElements = 65;
            reject(invalid);
            invalid = pso_desc;
            invalid.VS.BytecodeLength = 1024 * 1024 + 1;
            reject(invalid);
            OwnedStream truncated{pso_desc};
            odv::research::NativeVertexPipelineSource bad_size;
            require(!bad_size.prepare(D3D12_PIPELINE_STATE_STREAM_DESC{sizeof(truncated) - 1, &truncated}));
            truncated.ps.kind = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
            odv::research::NativeVertexPipelineSource duplicate;
            require(!duplicate.prepare(D3D12_PIPELINE_STATE_STREAM_DESC{sizeof(truncated), &truncated}));
            truncated.ps.kind = static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(63);
            odv::research::NativeVertexPipelineSource unknown;
            require(!unknown.prepare(D3D12_PIPELINE_STATE_STREAM_DESC{sizeof(truncated), &truncated}));
        }
        auto temporary_vs = vs, temporary_ps = ps;
        pso_desc.VS = {temporary_vs.data(), temporary_vs.size()};
        pso_desc.PS = {temporary_ps.data(), temporary_ps.size()};
        // A driver cache belongs to its original shader. The variant must
        // discard it without ever dereferencing this owned sentinel pointer.
        const std::byte cache_sentinel{0xAD};
        pso_desc.CachedPSO = {&cache_sentinel, 1};
        OwnedStream stream{pso_desc};
        odv::research::NativeVertexPipelineSource native_source;
        const D3D12_PIPELINE_STATE_STREAM_DESC stream_desc{sizeof(stream), &stream};
        require(use_stream ? native_source.prepare(stream_desc) : native_source.prepare(pso_desc));
        if (!retained_native)
            checked(use_stream ? odv::research::NativeVertexPipelinePayload::attach(pipelines[0].Get(), stream_desc)
                               : odv::research::NativeVertexPipelinePayload::attach(pipelines[0].Get(), pso_desc));
        require(odv::research::NativeVertexPipelinePayload::attach(pipelines[0].Get(), pso_desc) ==
                HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        auto retained_source = odv::research::load_native_vertex_source(pipelines[0].Get());
        require(retained_source != nullptr);
        std::cout << "payload_source=" << (retained_native ? "WRAPPER_NATIVE_CREATION" : "OWNED_DIRECT_ATTACHMENT")
                  << '\n';
        require(native_source.root() == root.Get() && native_source.vertex().BytecodeLength == vs.size() &&
                std::memcmp(native_source.vertex().pShaderBytecode, vs.data(), vs.size()) == 0 &&
                native_source.cleared_cache() && native_source.stream_source() == use_stream);
        require(!native_source.prepare(pso_desc));
        std::fill(temporary_vs.begin(), temporary_vs.end(), std::uint8_t{0});
        std::fill(temporary_ps.begin(), temporary_ps.end(), std::uint8_t{0});
        std::memset(&stream, 0, sizeof(stream));
        require(std::memcmp(native_source.vertex().pShaderBytecode, vs.data(), vs.size()) == 0);
        D3D12_SHADER_BYTECODE retained_vertex{};
        checked(retained_source->GetVertex(&retained_vertex));
        require(retained_vertex.BytecodeLength == vs.size() &&
                std::memcmp(retained_vertex.pShaderBytecode, vs.data(), vs.size()) == 0);
        std::cout << "native_source_controls=bounds,unknown,duplicate,stages,ownership,cache_discard\n";
        stage = "native_private_vs_variant";
        for (UINT i = 1; i < 4; ++i) {
            std::cout << "creating_variant=" << i << '\n';
            checked(retained_source->CreateVariant(device.Get(), i - 1, shaders[i].data(), shaders[i].size(),
                                                   &pipelines[i]));
        }
        Com<ID3D12PipelineState> rejected;
        require(retained_source->CreateVariant(device.Get(), 0, shaders[1].data(), shaders[1].size(), &rejected) ==
                    HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) &&
                !rejected);
        stage = "original_resources";
        const std::array<std::byte, 256> camera{};
        auto source = upload(device.Get(), camera.data(), camera.size());
        const std::array<UINT16, 6> indices{0, 1, 2, 2, 1, 3};
        auto index = upload(device.Get(), indices.data(), sizeof(indices));
        Arguments a{{index->GetGPUVirtualAddress(), sizeof(indices), DXGI_FORMAT_R16_UINT}, 1, {6, 1, 0, 0, 0}}, b = a;
        b.token = 2;
        std::array<std::byte, 112> argument_bytes{};
        argument_bytes.fill(std::byte{0xCD});
        std::memcpy(argument_bytes.data() + 16, &a, sizeof(a));
        std::memcpy(argument_bytes.data() + 56, &b, sizeof(b));
        auto argument_upload = upload(device.Get(), argument_bytes.data(), argument_bytes.size());
        auto arguments = gpu_buffer(device.Get(), argument_bytes.size());
        const std::array<UINT, 4> count_bytes{0, 1, 2, 0xFA123456};
        auto count_upload = upload(device.Get(), count_bytes.data(), sizeof(count_bytes));
        auto counts = gpu_buffer(device.Get(), sizeof(count_bytes));
        D3D12_INDIRECT_ARGUMENT_DESC indirect[3]{};
        indirect[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
        indirect[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        indirect[1].Constant = {0, 0, 1};
        indirect[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        const D3D12_COMMAND_SIGNATURE_DESC sig_desc{40, 3, indirect, 0};
        Com<ID3D12CommandSignature> signature;
        checked(device->CreateCommandSignature(&sig_desc, root.Get(), IID_PPV_ARGS(&signature)));
        odv::research::NativeIndirectDescription native_signature{};
        require(odv::research::describe_indirect_signature(&sig_desc, root.Get(), native_signature));
        require(odv::research::clip_probe_signature(native_signature));
        Com<ID3D12DescriptorHeap> rtv_heap;
        const D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                                                   0};
        checked(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap)));
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = 64;
        texture.Height = 16;
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
            UINT shader, argument_offset, maximum, count_offset, left_blank, right_blank;
            bool empty;
        };
        const std::array<Case, 11> cases{{{0, 16, 1, 4, 0, 0, false},
                                          {1, 16, 1, 4, 0, 0, false},
                                          {2, 16, 1, 4, 0, 4, false},
                                          {3, 16, 1, 4, 4, 0, false},
                                          {2, 56, 1, 4, 0, 2, false},
                                          {3, 56, 1, 4, 2, 0, false},
                                          {0, 56, 1, 4, 0, 0, false},
                                          {1, 56, 1, 4, 0, 0, false},
                                          {3, 16, 1, 0, 0, 0, true},
                                          {2, 16, 0, 4, 0, 0, true},
                                          {0, 16, 1, 4, 0, 0, false}}};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        const auto read_desc = buffer(cases.size() * 4096);
        Com<ID3D12Resource> readback;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &read_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr, IID_PPV_ARGS(&readback)));
        std::array<std::byte, 128> expected{};
        std::memcpy(expected.data(), argument_bytes.data(), 112);
        std::memcpy(expected.data() + 112, count_bytes.data(), 16);
        const auto input_read_desc = buffer(expected.size());
        Com<ID3D12Resource> input_readback;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &input_read_desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&input_readback)));
        Com<ID3D12CommandQueue> queue;
        // Query accounting detects a duplicated forwarding call even when its
        // pixels overwrite the same area and would otherwise hide the error.
        Com<ID3D12QueryHeap> statistics, occlusion;
        D3D12_QUERY_HEAP_DESC query_desc{D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, static_cast<UINT>(cases.size()), 0};
        checked(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&statistics)));
        query_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        checked(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&occlusion)));
        constexpr UINT64 query_stride = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) + sizeof(UINT64);
        const auto query_read_desc = buffer(cases.size() * query_stride);
        Com<ID3D12Resource> query_readback;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &query_read_desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&query_readback)));
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        Com<ID3D12CommandAllocator> allocator;
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList> list;
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&list)));
        stage = "recording";
        list->CopyBufferRegion(arguments.Get(), 0, argument_upload.Get(), 0, 112);
        list->CopyBufferRegion(counts.Get(), 0, count_upload.Get(), 0, 16);
        transition(list.Get(), arguments.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transition(list.Get(), counts.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        list->SetGraphicsRootSignature(root.Get());
        list->SetGraphicsRootConstantBufferView(1, source->GetGPUVirtualAddress());
        const D3D12_VIEWPORT viewport{0, 0, 64, 16, 0, 1};
        const D3D12_RECT scissor{0, 0, 64, 16};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        for (UINT i = 0; i < cases.size(); ++i) {
            const auto &c = cases[i];
            if (i == 0)
                list->SetPipelineState(pipelines[0].Get());
            const float black[4]{};
            list->ClearRenderTargetView(rtv, black, 0, nullptr);
            // Same original root, signature, CBV, index/argument/count buffers.
            // Only PSO changes; no root rebind or command-signature replacement.
            const odv::research::NativeIndirectCall call{signature.Get(),   c.maximum,    arguments.Get(),
                                                         c.argument_offset, counts.Get(), c.count_offset};
            list->BeginQuery(statistics.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, i);
            list->BeginQuery(occlusion.Get(), D3D12_QUERY_TYPE_OCCLUSION, i);
            if (c.shader)
                require(odv::research::record_clip_indirect(list.Get(), pipelines[0].Get(), pipelines[c.shader].Get(),
                                                            call, native_signature));
            else
                list->ExecuteIndirect(call.signature, call.maximum_count, call.argument_buffer, call.argument_offset,
                                      call.count_buffer, call.count_offset);
            list->EndQuery(occlusion.Get(), D3D12_QUERY_TYPE_OCCLUSION, i);
            list->EndQuery(statistics.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, i);
            list->ResolveQueryData(statistics.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, i, 1, query_readback.Get(),
                                   i * query_stride);
            list->ResolveQueryData(occlusion.Get(), D3D12_QUERY_TYPE_OCCLUSION, i, 1, query_readback.Get(),
                                   i * query_stride + sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
            transition(list.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Offset = i * 4096;
            dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 64, 16, 1, 256};
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = target.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            transition(list.Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        transition(list.Get(), arguments.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list.Get(), counts.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(input_readback.Get(), 0, arguments.Get(), 0, 112);
        list->CopyBufferRegion(input_readback.Get(), 112, counts.Get(), 0, 16);
        checked(list->Close());
        ID3D12CommandList *lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        Com<ID3D12Fence> fence;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        checked(queue->Signal(fence.Get(), 1));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(event != nullptr);
        checked(fence->SetEventOnCompletion(1, event));
        const DWORD waited = WaitForSingleObject(event, 15000);
        CloseHandle(event);
        require(waited == WAIT_OBJECT_0 && fence->GetCompletedValue() >= 1 && fence->GetCompletedValue() != UINT64_MAX);
        stage = "debug_validation";
        if (info)
            for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T bytes = 0;
                checked(info->GetMessage(i, nullptr, &bytes));
                std::vector<std::byte> storage(bytes);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                checked(info->GetMessage(i, message, &bytes));
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                    std::cerr << "D3D12 id=" << message->ID << ' ' << message->pDescription << '\n';
                require(message->Severity > D3D12_MESSAGE_SEVERITY_ERROR);
            }
        stage = "input_preservation";
        unchanged(source.Get(), camera.data(), camera.size());
        unchanged(index.Get(), indices.data(), sizeof(indices));
        unchanged(argument_upload.Get(), argument_bytes.data(), 112);
        unchanged(count_upload.Get(), count_bytes.data(), 16);
        unchanged(input_readback.Get(), expected.data(), 128);
        stage = "pixels";
        void *mapped = nullptr;
        const D3D12_RANGE read_range{0, SIZE_T(read_desc.Width)};
        checked(readback->Map(0, &read_range, &mapped));
        bool pixels = true;
        for (UINT i = 0; i < cases.size(); ++i)
            for (UINT y = 0; y < 16; ++y)
                for (UINT x = 0; x < 64; ++x) {
                    const auto &c = cases[i];
                    const bool red = !c.empty && x >= c.left_blank && x < 64 - c.right_blank;
                    const auto *pixel = static_cast<const unsigned char *>(mapped) + i * 4096 + y * 256 + x * 4;
                    const bool same =
                        pixel[0] == (red ? 255 : 0) && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == (red ? 255 : 0);
                    if (!same && pixels)
                        std::cerr << "case=" << i << " x=" << x << " y=" << y << " actual=" << UINT(pixel[0]) << ','
                                  << UINT(pixel[1]) << ',' << UINT(pixel[2]) << ',' << UINT(pixel[3]) << '\n';
                    pixels &= same;
                }
        const D3D12_RANGE no_write{};
        readback->Unmap(0, &no_write);
        require(pixels);
        stage = "single_forward_query_accounting";
        const D3D12_RANGE query_range{0, SIZE_T(query_read_desc.Width)};
        checked(query_readback->Map(0, &query_range, &mapped));
        bool queries = true;
        for (UINT i = 0; i < cases.size(); ++i) {
            D3D12_QUERY_DATA_PIPELINE_STATISTICS stats{};
            UINT64 samples = 0;
            const auto *bytes = static_cast<const std::byte *>(mapped) + i * query_stride;
            std::memcpy(&stats, bytes, sizeof(stats));
            std::memcpy(&samples, bytes + sizeof(stats), sizeof(samples));
            const auto &c = cases[i];
            queries &= stats.IAVertices == (c.empty ? 0u : 6u) && stats.IAPrimitives == (c.empty ? 0u : 2u) &&
                       samples == (c.empty ? 0u : (64u - c.left_blank - c.right_blank) * 16u);
        }
        query_readback->Unmap(0, &no_write);
        require(queries);
        std::cout << "query_accounting=PASS_SINGLE_FORWARD_PIPELINE_STATISTICS_AND_OCCLUSION\n";
        std::cout << "PASS: validated private DXIL clip-X geometry probe; 11 cases x1024 pixels; near/far4/2-pixel "
                     "displacement; original root/signature/buffers unchanged; identity and original PSO restoration; "
                     "GPU count0/max0; debug="
                  << (validation ? "ENABLED" : "UNAVAILABLE")
                  << "; native_source=" << (use_stream ? "STREAM" : "LEGACY") << "; game=NOT_RUN; headset=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "stage=" << stage << ' ' << error.what() << '\n';
        // This executable accepts owned fixtures only. Keep creation errors in
        // its test log rather than losing the debug message during unwinding.
        if (info)
            for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T bytes = 0;
                if (FAILED(info->GetMessage(i, nullptr, &bytes)) || bytes > 1024 * 1024)
                    break;
                std::vector<std::byte> storage(bytes);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                if (SUCCEEDED(info->GetMessage(i, message, &bytes)) &&
                    message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                    std::cerr << "D3D12 id=" << message->ID << ' ' << message->pDescription << '\n';
            }
        return 1;
    }
}
