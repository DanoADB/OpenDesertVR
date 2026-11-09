// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <d3d12.h>

namespace odv::research {
// Separate ABI from the pinned barrier/after-execute contract. Notifications
// follow actual native calls, including SO binds whose generic resolution fails.
// Arrays are borrowed only for the synchronous callback. No application calls
// are replaced, suppressed or changed by this sink.
inline constexpr std::uint8_t native_draw_events_guid[16] = {0x6e, 0x1c, 0xd8, 0x45, 0x1a, 0x9b, 0x4f, 0x72,
                                                             0x8a, 0xf9, 0x39, 0x21, 0x61, 0x08, 0x34, 0xdd};
inline constexpr std::uint32_t native_draw_capabilities = 7; // SO, query, render pass.
struct NativeDrawEvents final {
    std::uint32_t size = sizeof(NativeDrawEvents);
    std::uint32_t version = 1;
    void *context = nullptr;
    void(__cdecl *stream_output)(void *, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *) noexcept = nullptr;
    void(__cdecl *query)(void *, ID3D12QueryHeap *, D3D12_QUERY_TYPE, UINT, bool) noexcept = nullptr;
    void(__cdecl *render_pass)(void *, bool, D3D12_RENDER_PASS_FLAGS) noexcept = nullptr;
    void(__cdecl *capabilities)(void *, std::uint32_t) noexcept = nullptr;
};
static_assert(sizeof(void *) == 8 && sizeof(NativeDrawEvents) == 48);
template <class CommandList> NativeDrawEvents *get_native_draw_events(CommandList *list) noexcept {
    if (!list)
        return nullptr;
    std::uint64_t value = 0;
    list->get_private_data(native_draw_events_guid, &value);
    auto *events = reinterpret_cast<NativeDrawEvents *>(value);
    return events && events->size == sizeof(*events) && events->version == 1 ? events : nullptr;
}
} // namespace odv::research
