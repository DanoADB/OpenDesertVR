// SPDX-License-Identifier: MIT
#pragma once
#include "../shader-consumer-audit/static_cbv_access.hpp"
#include <bit>
#include <d3d12.h>
#include <dxcapi.h>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <windows.h>
#include <wrl/client.h>

namespace odv::research {
enum class ClipShift { left, identity, right };
// Fixed research probe: add a constant to homogeneous clip.x, leaving w and
// every other output unchanged. This is NOT a complete stereo/game adapter.
// No source/descriptor/root/argument buffer is modified. Text stays in process.
inline std::optional<std::string> shift_clip_x_text(std::string_view text, ClipShift shift) {
    using namespace cbv_detail;
    if (text.empty() || text.size() > 1024 * 1024 || text.find("odv_clip_probe_x") != text.npos)
        return {};
    std::map<std::string, std::vector<std::string>> nodes;
    static const std::regex metadata(R"(^(![A-Za-z0-9_.]+) = !\{(.*)\}$)");
    for (std::size_t p = 0; p < text.size();) {
        auto end = text.find('\n', p);
        if (end == text.npos)
            end = text.size();
        auto line = trim(text.substr(p, end - p));
        p = end + 1;
        if (line.size() > 16384)
            return {};
        std::smatch match;
        if (std::regex_match(line, match, metadata)) {
            if (nodes.size() >= 4096 || nodes.contains(match[1].str()))
                return {};
            nodes.emplace(match[1].str(), fields(match[2].str()));
        }
    }
    const auto get = [&](const std::string &key) -> const std::vector<std::string> & {
        static const std::vector<std::string> empty;
        auto found = nodes.find(key);
        return found == nodes.end() ? empty : found->second;
    };
    const auto &model_ref = get("!dx.shaderModel"), &entries = get("!dx.entryPoints");
    if (model_ref.size() != 1 || entries.size() != 1)
        return {};
    const auto &model = get(model_ref[0]), &entry = get(entries[0]);
    if (model.size() != 3 || model[0] != "!\"vs\"" || entry.size() != 5)
        return {};
    const auto &signature = get(entry[2]);
    if (signature.size() != 3)
        return {};
    std::uint32_t position = 0;
    unsigned positions = 0;
    for (const auto &reference : get(signature[1])) {
        const auto &element = get(reference);
        if (element.size() != 11)
            return {};
        if (element[1] != "!\"SV_Position\"" && element[1] != "!\"SV_POSITION\"")
            continue;
        if (element[2] != "i8 9" || element[3] != "i8 3" || element[6] != "i32 1" || element[7] != "i8 4" ||
            !i32(element[0], position))
            return {};
        ++positions;
    }
    if (positions != 1)
        return {};
    static const std::regex store(
        R"(^(\s*call void @dx.op.storeOutput\.f32\(i32 5, i32 ([0-9]+), i32 ([0-9]+), i8 ([0-9]+), float )([A-Za-z0-9_%+.-]+)(\).*)$)");
    std::string output;
    unsigned changed = 0;
    const char *delta = shift == ClipShift::left    ? "-1.250000e-01"
                        : shift == ClipShift::right ? "1.250000e-01"
                                                    : "0.000000e+00";
    for (std::size_t p = 0; p < text.size();) {
        auto end = text.find('\n', p);
        if (end == text.npos)
            end = text.size();
        std::string line(text.substr(p, end - p));
        const bool newline = end < text.size();
        p = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::smatch match;
        if (std::regex_match(line, match, store)) {
            std::uint32_t id = 0, row = 0, column = 0;
            if (!number(match[2].str(), id) || !number(match[3].str(), row) || !number(match[4].str(), column))
                return {};
            if (id == position && column == 0) {
                if (row != 0 || ++changed != 1)
                    return {};
                output += "  %odv_clip_probe_x = fadd float " + match[5].str() + ", " + delta + "\n";
                line = match[1].str() + "%odv_clip_probe_x" + match[6].str();
            }
        }
        output += line;
        if (newline)
            output += '\n';
    }
    return changed == 1 ? std::optional<std::string>(std::move(output)) : std::nullopt;
}

class ClipShiftCompiler final {
    HMODULE compiler_ = nullptr, validator_ = nullptr;
    DxcCreateInstanceProc create_ = nullptr, validate_create_ = nullptr;
    static void checked(HRESULT hr) {
        if (FAILED(hr))
            throw std::runtime_error("dxil_clip_probe_rejected");
    }
    static Microsoft::WRL::ComPtr<IDxcBlob> result(IDxcOperationResult *operation) {
        HRESULT status = E_FAIL;
        checked(operation->GetStatus(&status));
        checked(status);
        Microsoft::WRL::ComPtr<IDxcBlob> blob;
        checked(operation->GetResult(&blob));
        return blob;
    }

  public:
    explicit ClipShiftCompiler(const std::filesystem::path &directory) {
        if (!directory.is_absolute())
            throw std::runtime_error("absolute_sdk_directory_required");
        compiler_ = LoadLibraryExW((directory / L"dxcompiler.dll").c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        validator_ = LoadLibraryExW((directory / L"dxil.dll").c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (compiler_)
            create_ = std::bit_cast<DxcCreateInstanceProc>(GetProcAddress(compiler_, "DxcCreateInstance"));
        if (validator_)
            validate_create_ = std::bit_cast<DxcCreateInstanceProc>(GetProcAddress(validator_, "DxcCreateInstance"));
        if (!create_ || !validate_create_) {
            if (validator_)
                FreeLibrary(validator_);
            if (compiler_)
                FreeLibrary(compiler_);
            throw std::runtime_error("sdk_compiler_validator_unavailable");
        }
    }
    ~ClipShiftCompiler() {
        FreeLibrary(validator_);
        FreeLibrary(compiler_);
    }
    ClipShiftCompiler(const ClipShiftCompiler &) = delete;
    ClipShiftCompiler &operator=(const ClipShiftCompiler &) = delete;
    std::vector<std::uint8_t> compile(std::span<const std::uint8_t> original, ClipShift shift) const {
        if (original.size() < 32 || original.size() > 1024 * 1024)
            throw std::runtime_error("shader_input_bound");
        Microsoft::WRL::ComPtr<IDxcUtils> utils;
        checked(create_(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
        Microsoft::WRL::ComPtr<IDxcCompiler> compiler;
        checked(create_(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> source, text;
        checked(utils->CreateBlob(original.data(), static_cast<UINT32>(original.size()), DXC_CP_ACP, &source));
        checked(compiler->Disassemble(source.Get(), &text));
        if (text->GetBufferSize() > 1024 * 1024)
            throw std::runtime_error("shader_text_bound");
        std::string_view view(static_cast<const char *>(text->GetBufferPointer()), text->GetBufferSize());
        if (!view.empty() && view.back() == '\0')
            view.remove_suffix(1);
        auto shifted = shift_clip_x_text(view, shift);
        if (!shifted)
            throw std::runtime_error("unsupported_position_store");
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> assembly;
        checked(utils->CreateBlob(shifted->data(), static_cast<UINT32>(shifted->size()), DXC_CP_UTF8, &assembly));
        Microsoft::WRL::ComPtr<IDxcAssembler> assembler;
        checked(create_(CLSID_DxcAssembler, IID_PPV_ARGS(&assembler)));
        Microsoft::WRL::ComPtr<IDxcOperationResult> assembled;
        checked(assembler->AssembleToContainer(assembly.Get(), &assembled));
        auto binary = result(assembled.Get());
        Microsoft::WRL::ComPtr<IDxcValidator> validator;
        checked(validate_create_(CLSID_DxcValidator, IID_PPV_ARGS(&validator)));
        Microsoft::WRL::ComPtr<IDxcOperationResult> validated;
        checked(validator->Validate(binary.Get(), DxcValidatorFlags_InPlaceEdit, &validated));
        auto accepted = result(validated.Get());
        if (accepted->GetBufferSize() < 32 || accepted->GetBufferSize() > 1024 * 1024)
            throw std::runtime_error("output_shader_bound");
        const auto *bytes = static_cast<const std::uint8_t *>(accepted->GetBufferPointer());
        return {bytes, bytes + accepted->GetBufferSize()};
    }
};
} // namespace odv::research
