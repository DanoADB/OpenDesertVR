// SPDX-License-Identifier: MIT
#include "../renodx-owned/native_root_signature.hpp"
#include "private_cbv_layout.hpp"
#include "private_indirect_signature.hpp"
#include <dxgi1_4.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>
template <class T> using Com = Microsoft::WRL::ComPtr<T>;
static void require(bool value) {
    if (!value)
        throw std::runtime_error("owned_root_blob_control_failed");
}
static void checked(HRESULT value) { require(SUCCEEDED(value)); }
static void private_indirect_control(ID3D12Device *device) {
    D3D12_DESCRIPTOR_RANGE1 range{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,           1, 18, 35,
                                  D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0};
    D3D12_ROOT_PARAMETER1 parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[0].Constants = {0, 99, 1};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[1].DescriptorTable = {1, &range};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC source{};
    source.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    source.Desc_1_1 = {2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    Com<ID3DBlob> serialized, errors;
    checked(D3D12SerializeVersionedRootSignature(&source, &serialized, &errors));
    Com<ID3D12RootSignature> original_root, private_root;
    checked(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                        IID_PPV_ARGS(&original_root)));
    odv::research::PrivateCbvLayout layout;
    require(layout.prepare(source.Desc_1_1, 1, 18, 35, true));
    serialized.Reset();
    checked(D3D12SerializeVersionedRootSignature(layout.description(), &serialized, &errors));
    checked(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                        IID_PPV_ARGS(&private_root)));
    D3D12_INDIRECT_ARGUMENT_DESC arguments[3]{};
    arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    arguments[1].Constant = {0, 0, 1};
    arguments[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    const D3D12_COMMAND_SIGNATURE_DESC signature{40, 3, arguments, 0};
    Com<ID3D12CommandSignature> original;
    checked(device->CreateCommandSignature(&signature, original_root.Get(), IID_PPV_ARGS(&original)));
    odv::research::PrivateIndirectSignature clone;
    require(!clone.prepare(device, original.Get(), original_root.Get(), private_root.Get(), 1));
    odv::research::NativeIndirectDescription metadata;
    require(odv::research::describe_indirect_signature(&signature, original_root.Get(), metadata));
    checked(original->SetPrivateData(odv::research::native_signature_metadata_guid, sizeof(metadata), &metadata));
    require(!clone.prepare(device, original.Get(), original_root.Get(), private_root.Get(), 0));
    require(!clone.prepare(device, original.Get(), private_root.Get(), private_root.Get(), 1));
    require(clone.prepare(device, original.Get(), original_root.Get(), private_root.Get(), 1));
    require(clone.replacement() && clone.replacement() != original.Get());
    odv::research::NativeIndirectDescription after;
    require(odv::research::load_indirect_description(original.Get(), after) &&
            std::memcmp(&metadata, &after, sizeof(after)) == 0);
    std::cout << "PASS: native private-root signature clone uses exact IBV/one root0 word/DrawIndexed stride40; "
                 "original metadata unchanged; missing tag and private-argument mutation rejected; "
                 "GPU replay=NOT_RUN; game=NOT_RUN\n";
}
static void private_layout_control(ID3D12Device *device) {
    D3D12_DESCRIPTOR_RANGE1 ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 3, 2, 35, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 10},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 35, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
         D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}};
    const auto original = std::to_array(ranges);
    D3D12_ROOT_PARAMETER1 parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[0].DescriptorTable = {2, ranges};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].Constants = {0, 99, 1};
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 9;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC1 source{2, parameters, 1, &sampler,
                                      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    odv::research::PrivateCbvLayout layout;
    require(!layout.prepare(source, 0, 3, 35, false)); // Array access is not implicitly safe.
    require(layout.prepare(source, 0, 3, 35, true));   // Authored non-array CBV control.
    require(layout.private_index() == 2 && layout.dword_cost() == 4);
    require(std::memcmp(original.data(), ranges, sizeof(ranges)) == 0);
    Com<ID3DBlob> serialized, errors;
    checked(D3D12SerializeVersionedRootSignature(layout.description(), &serialized, &errors));
    Com<ID3D12RootSignature> root;
    checked(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                        IID_PPV_ARGS(&root)));
    Com<ID3D12VersionedRootSignatureDeserializer> decoded;
    checked(D3D12CreateVersionedRootSignatureDeserializer(serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                          IID_PPV_ARGS(&decoded)));
    const auto &actual = decoded->GetUnconvertedRootSignatureDesc()->Desc_1_1;
    const auto &table = actual.pParameters[0].DescriptorTable;
    require(actual.NumParameters == 3 && actual.Flags == source.Flags && actual.NumStaticSamplers == 1 &&
            actual.pStaticSamplers[0].ShaderRegister == 9 && table.NumDescriptorRanges == 3);
    require(table.pDescriptorRanges[0].BaseShaderRegister == 2 &&
            table.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart == 10 &&
            table.pDescriptorRanges[1].BaseShaderRegister == 4 &&
            table.pDescriptorRanges[1].OffsetInDescriptorsFromTableStart == 12 &&
            table.pDescriptorRanges[2].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
            table.pDescriptorRanges[2].OffsetInDescriptorsFromTableStart == 13 &&
            table.pDescriptorRanges[2].Flags == D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC &&
            actual.pParameters[2].Descriptor.Flags == D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
    parameters[1].Constants.Num32BitValues = 63;
    require(!layout.prepare(source, 0, 3, 35, true) && !layout.description());
    std::cout << "PASS: native derived root preserves split-table offsets, append offsets, flags and static sampler; "
                 "original ranges unchanged; missing array proof and DWORD overflow rejected; game=NOT_RUN\n";
}
int main() {
    try {
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        checked(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        private_layout_control(device.Get());
        private_indirect_control(device.Get());
        D3D12_ROOT_PARAMETER1 parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameter.Descriptor = {7, 3, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = 1;
        desc.Desc_1_1.pParameters = &parameter;
        desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Com<ID3DBlob> serialized, errors;
        checked(D3D12SerializeVersionedRootSignature(&desc, &serialized, &errors));
        Com<ID3D12RootSignature> root;
        checked(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                            IID_PPV_ARGS(&root)));
        UINT size = 0;
        require(!odv::research::root_signature_storage_size(root.Get(), size));
        require(!odv::research::store_root_signature_blob(root.Get(), 0, nullptr, 4));
        require(!odv::research::store_root_signature_blob(root.Get(), 0, serialized->GetBufferPointer(),
                                                          odv::research::maximum_root_blob_bytes + 1));
        require(odv::research::store_root_signature_blob(root.Get(), 0, serialized->GetBufferPointer(),
                                                         serialized->GetBufferSize()));
        require(odv::research::root_signature_storage_size(root.Get(), size));
        std::vector<std::byte> storage(size);
        UINT node = 0, bytes = 0;
        const void *blob = nullptr;
        require(!odv::research::load_root_signature_blob(root.Get(), storage.data(), size - 1, node, blob, bytes));
        require(odv::research::load_root_signature_blob(root.Get(), storage.data(), size, node, blob, bytes));
        require(node == 0 && bytes == serialized->GetBufferSize() &&
                std::memcmp(blob, serialized->GetBufferPointer(), bytes) == 0);
        Com<ID3D12VersionedRootSignatureDeserializer> decoded;
        checked(D3D12CreateVersionedRootSignatureDeserializer(blob, bytes, IID_PPV_ARGS(&decoded)));
        const auto *actual = decoded->GetUnconvertedRootSignatureDesc();
        require(actual->Version == desc.Version && actual->Desc_1_1.NumParameters == 1 &&
                actual->Desc_1_1.Flags == desc.Desc_1_1.Flags &&
                actual->Desc_1_1.pParameters[0].Descriptor.ShaderRegister == 7 &&
                actual->Desc_1_1.pParameters[0].Descriptor.RegisterSpace == 3 &&
                actual->Desc_1_1.pParameters[0].Descriptor.Flags == D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
        const UINT malformed = 0;
        checked(root->SetPrivateData(odv::research::native_root_signature_guid, sizeof(malformed), &malformed));
        require(!odv::research::root_signature_storage_size(root.Get(), size));
        std::cout << "PASS: owned native root object retains exact version/flags/registers/bytes; bounded malformed "
                     "and missing tags rejected; files_written=0; wrapper_capture=NOT_RUN; game=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
