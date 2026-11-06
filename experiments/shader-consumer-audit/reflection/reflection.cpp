// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <fcntl.h>
#include <filesystem>
#include <io.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace {
using Microsoft::WRL::ComPtr;
struct Rejected {
    const char *reason;
};
void require(bool condition, const char *reason) {
    if (!condition)
        throw Rejected{reason};
}
struct Module {
    HMODULE handle{};
    ~Module() {
        if (handle)
            FreeLibrary(handle);
    }
};
const char *type_name(D3D_SHADER_VARIABLE_TYPE type) {
    switch (type) {
    case D3D_SVT_UINT:
        return "uint";
    case D3D_SVT_INT:
        return "int";
    case D3D_SVT_FLOAT:
        return "float";
    case D3D_SVT_DOUBLE:
        return "double";
    case D3D_SVT_BOOL:
        return "bool";
    default:
        return "other";
    }
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 2, "expected_sdk_library_argument");
        const std::filesystem::path library(argv[1]);
        require(library.is_absolute() && library.filename() == L"dxcompiler.dll", "unexpected_sdk_library_path");
        require(_setmode(_fileno(stdin), _O_BINARY) != -1, "binary_input_unavailable");
        constexpr std::size_t limit = 1024 * 1024;
        std::vector<char> data(limit + 1);
        std::cin.read(data.data(), static_cast<std::streamsize>(data.size()));
        const auto size = static_cast<std::size_t>(std::cin.gcount());
        require(size >= 32 && size <= limit && std::memcmp(data.data(), "DXBC", 4) == 0,
                "invalid_bounded_container_input");
        // The parent supplies structurally qualified bytes. DXC operates only
        // in this helper process; no shader is compiled, linked or executed.
        Module module{
            LoadLibraryExW(library.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
        require(module.handle != nullptr, "sdk_library_load_failed");
        const auto address = GetProcAddress(module.handle, "DxcCreateInstance");
        require(address != nullptr, "sdk_factory_unavailable");
        const auto create = std::bit_cast<DxcCreateInstanceProc>(address);
        ComPtr<IDxcUtils> utils;
        require(SUCCEEDED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))), "sdk_utils_unavailable");
        DxcBuffer buffer{data.data(), size, 0};
        ComPtr<ID3D12ShaderReflection> reflection;
        require(SUCCEEDED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection))), "reflection_unavailable");
        D3D12_SHADER_DESC shader{};
        require(SUCCEEDED(reflection->GetDesc(&shader)), "shader_description_unavailable");
        require(shader.ConstantBuffers <= 64 && shader.BoundResources <= 512, "shader_counts_outside_bounds");
        constexpr std::array<UINT, 9> queries{0x20, 0x30, 0x80, 0x90, 0x3e0, 0x420, 0x4e0, 0x860, 0xac0};
        std::ostringstream records;
        UINT variable_count = 0, matches = 0, constant_buffers = 0;
        for (UINT ordinal = 0; ordinal < shader.ConstantBuffers; ++ordinal) {
            auto *cb = reflection->GetConstantBufferByIndex(ordinal);
            require(cb != nullptr, "constant_buffer_unavailable");
            D3D12_SHADER_BUFFER_DESC description{};
            require(SUCCEEDED(cb->GetDesc(&description)), "buffer_description_unavailable");
            require(description.Variables <= 2048 && description.Size <= 65536, "buffer_counts_outside_bounds");
            variable_count += description.Variables;
            require(variable_count <= 4096, "total_variable_count_outside_bounds");
            if (description.Type != D3D_CT_CBUFFER)
                continue;
            ++constant_buffers;
            for (UINT i = 0; i < description.Variables; ++i) {
                auto *variable = cb->GetVariableByIndex(i);
                require(variable != nullptr, "variable_unavailable");
                D3D12_SHADER_VARIABLE_DESC value{};
                require(SUCCEEDED(variable->GetDesc(&value)), "variable_description_unavailable");
                require(value.StartOffset <= description.Size && value.Size <= description.Size - value.StartOffset,
                        "variable_span_outside_buffer");
                for (auto offset : queries) {
                    if (offset < value.StartOffset || offset - value.StartOffset >= value.Size)
                        continue;
                    auto *type = variable->GetType();
                    require(type != nullptr, "variable_type_unavailable");
                    D3D12_SHADER_TYPE_DESC shape{};
                    require(SUCCEEDED(type->GetDesc(&shape)), "type_description_unavailable");
                    require(++matches <= 512, "query_match_count_outside_bounds");
                    if (matches != 1)
                        records << ',';
                    records << "{\"query_offset\":" << offset << ",\"buffer_ordinal\":" << ordinal
                            << ",\"declaration_offset\":" << value.StartOffset
                            << ",\"declaration_bytes\":" << value.Size
                            << ",\"exact_start\":" << (offset == value.StartOffset ? "true" : "false") << ",\"type\":\""
                            << type_name(shape.Type) << "\",\"class\":" << static_cast<UINT>(shape.Class)
                            << ",\"rows\":" << shape.Rows << ",\"columns\":" << shape.Columns
                            << ",\"elements\":" << shape.Elements << ",\"members\":" << shape.Members
                            << ",\"compiler_used\":" << ((value.uFlags & D3D_SVF_USED) ? "true" : "false") << '}';
                }
            }
        }
        std::ostringstream output;
        output << "{\"schema_version\":1,\"status\":\"OBSERVED_REFLECTION_METADATA\",\"constant_buffers\":"
               << constant_buffers << ",\"bound_resources\":" << shader.BoundResources
               << ",\"variables\":" << variable_count << ",\"query_matches\":[" << records.str()
               << "],\"scope\":\"Compiler reflection declarations only. No names or IR exported. Offset matches and "
                  "compiler-used flags do not establish live bindings, full byte consumption, CPU mapping, frame "
                  "association or camera semantics. No GPU execution.\"}\n";
        const auto text = output.str();
        require(text.size() <= 128 * 1024, "output_size_outside_bounds");
        std::cout << text;
        return 0;
    } catch (const Rejected &error) {
        std::cout << "{\"status\":\"REJECTED\",\"reason\":\"" << error.reason << "\"}\n";
    } catch (...) {
        std::cout << "{\"status\":\"REJECTED\",\"reason\":\"helper_failure\"}\n";
    }
    return 3;
}
