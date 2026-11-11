// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <windows.h>

namespace odv::research {
// Process-local, object-owned copy of the EXACT forwarded root-signature blob.
// Needed to preserve flags/versions/static samplers when preparing a private
// binding layout. Never export this payload through MCP, reports or files.
inline constexpr GUID native_root_signature_guid = {
    0x4c982a31, 0x75b6, 0x47a2, {0x81, 0xc0, 0x21, 0xc8, 0x42, 0xbb, 0x0a, 0xe7}};
inline constexpr UINT maximum_root_blob_bytes = 65536;
struct NativeRootHeader final {
    UINT magic, version, total_bytes, node_mask, blob_bytes;
};
inline bool store_root_signature_blob(ID3D12RootSignature *signature, UINT node_mask, const void *blob,
                                      SIZE_T bytes) noexcept {
    if (!signature || !blob || !bytes || bytes > maximum_root_blob_bytes)
        return false;
    const UINT total = sizeof(NativeRootHeader) + static_cast<UINT>(bytes);
    void *copy = HeapAlloc(GetProcessHeap(), 0, total);
    if (!copy)
        return false;
    const NativeRootHeader header{0x4f445652, 1, total, node_mask, static_cast<UINT>(bytes)};
    std::memcpy(copy, &header, sizeof(header));
    std::memcpy(static_cast<std::uint8_t *>(copy) + sizeof(header), blob, bytes);
    const auto result = signature->SetPrivateData(native_root_signature_guid, total, copy);
    HeapFree(GetProcessHeap(), 0, copy);
    return SUCCEEDED(result);
}
inline bool root_signature_storage_size(ID3D12RootSignature *signature, UINT &size) noexcept {
    size = 0;
    return signature && SUCCEEDED(signature->GetPrivateData(native_root_signature_guid, &size, nullptr)) &&
           size > sizeof(NativeRootHeader) && size <= sizeof(NativeRootHeader) + maximum_root_blob_bytes;
}
inline bool load_root_signature_blob(ID3D12RootSignature *signature, void *storage, UINT capacity, UINT &node_mask,
                                     const void *&blob, UINT &bytes) noexcept {
    blob = nullptr;
    bytes = node_mask = 0;
    UINT size = 0;
    if (!storage || !root_signature_storage_size(signature, size) || size > capacity)
        return false;
    if (FAILED(signature->GetPrivateData(native_root_signature_guid, &size, storage)))
        return false;
    NativeRootHeader header{};
    std::memcpy(&header, storage, sizeof(header));
    if (header.magic != 0x4f445652 || header.version != 1 || header.total_bytes != size || !header.blob_bytes ||
        header.blob_bytes != size - sizeof(header))
        return false;
    node_mask = header.node_mask;
    bytes = header.blob_bytes;
    blob = static_cast<const std::uint8_t *>(storage) + sizeof(header);
    return true;
}
} // namespace odv::research
