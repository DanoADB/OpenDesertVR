// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <d3d12.h>

namespace odv::research {
// Private, versioned interop contract for a separately built research wrapper.
// Register on a command-list API object's private data; clear before destroying
// the sink. Notifications are synchronous, after the native barrier recording.
// Pointers/arrays are borrowed for that callback only; never retain the arrays.
inline constexpr std::uint8_t native_barrier_events_guid[16] = {0x8b, 0x53, 0x7e, 0x1a, 0x5d, 0x42, 0x48, 0x93,
                                                                0xb5, 0x77, 0xb3, 0x63, 0x0c, 0xdd, 0x98, 0x21};
enum class NativeStateInvalidation : std::uint32_t { reset = 1, bundle = 2, clear_state = 3, protected_session = 4 };
struct NativeBarrierEvents final {
    std::uint32_t size = sizeof(NativeBarrierEvents);
    std::uint32_t version = 1;
    void *context = nullptr;
    void(__cdecl *legacy)(void *, UINT, const D3D12_RESOURCE_BARRIER *) noexcept = nullptr;
    void(__cdecl *enhanced)(void *, UINT32, const D3D12_BARRIER_GROUP *) noexcept = nullptr;
    void(__cdecl *invalidate)(void *, NativeStateInvalidation) noexcept = nullptr;
    // Native queue execution has returned. Signal a covering fence here, never
    // in the before-execute event. Command-list identity is supplied by context.
    void(__cdecl *after_execute)(void *, ID3D12CommandQueue *) noexcept = nullptr;
};
template <class CommandList> NativeBarrierEvents *get_native_barrier_events(CommandList *list) noexcept {
    if (!list)
        return nullptr;
    std::uint64_t value = 0;
    list->get_private_data(native_barrier_events_guid, &value);
    auto *events = reinterpret_cast<NativeBarrierEvents *>(value);
    return events && events->size == sizeof(*events) && events->version == 1 ? events : nullptr;
}
} // namespace odv::research
