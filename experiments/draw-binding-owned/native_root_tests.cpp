// SPDX-License-Identifier: MIT
#include "../renodx-owned/native_root_signature.hpp"
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
int main() {
    try {
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> adapter;
        checked(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
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
