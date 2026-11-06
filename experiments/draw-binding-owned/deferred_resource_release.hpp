// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <d3d12.h>
#include <mutex>
#include <wrl/client.h>

namespace odv::research {
// Final Release can invoke an interop layer's resource-destruction callbacks.
// Transfer completed/retired resources here while holding capture locks; drain
// only at a call site outside ALL capture and device-data locks, device alive.
class DeferredResourceRelease final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;

  public:
    static constexpr std::size_t capacity = 64;

  private:
    struct State {
        std::mutex mutex;
        std::array<Com<ID3D12Resource>, capacity> resources;
        ~State() {
            // A missing explicit drain must not call into an unloading addon.
            // Bounded quarantine; callers retain ownership on queue overflow.
            for (auto &resource : resources)
                resource.Detach();
        }
    };
    static State &state() {
        static State value;
        return value;
    }

  public:
    // All-or-none transfer. No AddRef/Release or allocation under the mutex.
    static bool defer(Com<ID3D12Resource> &first, Com<ID3D12Resource> &second) noexcept {
        auto &value = state();
        const std::lock_guard lock(value.mutex);
        const std::size_t needed = (first ? 1u : 0u) + (second ? 1u : 0u);
        std::size_t free = 0;
        for (const auto &resource : value.resources)
            free += resource ? 0u : 1u;
        if (free < needed)
            return false;
        for (auto &resource : value.resources) {
            if (resource)
                continue;
            if (first)
                resource.Swap(first);
            else if (second)
                resource.Swap(second);
        }
        return true;
    }
    static bool defer(Com<ID3D12Resource> &resource) noexcept {
        Com<ID3D12Resource> empty;
        return defer(resource, empty);
    }
    static void drain() noexcept {
        std::array<Com<ID3D12Resource>, capacity> release;
        auto &value = state();
        {
            const std::lock_guard lock(value.mutex);
            release.swap(value.resources);
        }
        // Destruction outside the queue mutex and outside caller locks.
    }
};
} // namespace odv::research
