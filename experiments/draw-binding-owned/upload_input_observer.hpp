// SPDX-License-Identifier: MIT
#pragma once
#include "owned_constant_copy.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>

namespace odv::research {
// Bounded observation of the FIRST covering UPLOAD input copied to an observed
// DEFAULT candidate after registration. Not a later per-draw DEFAULT snapshot.
class UploadInputObserver final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    using Clock = std::chrono::steady_clock;

  public:
    struct Entry {
      private:
        friend class UploadInputObserver;
        Com<ID3D12Resource> target, source;
        UINT64 target_offset = 0, source_offset = 0, capture = 0;
        std::size_t bytes = 0;
        std::uintptr_t list = 0;
        std::uintptr_t device = 0;
        Clock::time_point deadline;
        ConstantSnapshot::SourceInfo source_info;
        std::unique_ptr<OwnedConstantCopy> copy;
        const char *state = "WAITING_FOR_UPLOAD";
        bool matched = false, submitted = false, reset_pending = false, failed = false, device_closed = false;
    };
    using Ticket = std::shared_ptr<Entry>;
    struct Sample {
        const char *state = "NOT_CAPTURED";
        ConstantSnapshot::SourceInfo source;
        std::vector<std::byte> bytes;
    };

  private:
    std::mutex mutex_;
    std::vector<Ticket> entries_;
    UINT64 capture_ = 0;
    static bool poll(const Ticket &entry) {
        if (!entry->submitted || entry->failed || entry->device_closed)
            return false;
        try {
            if (entry->copy->read_completed()) {
                entry->state = "READY_UPLOAD_INPUT";
                entry->source.Reset();
                return true;
            }
        } catch (...) {
            entry->failed = true;
            entry->state = "COPY_FAILED";
        }
        return false;
    }

  public:
    void begin_capture() {
        const std::lock_guard lock(mutex_);
        ++capture_;
        std::erase_if(entries_, [](const Ticket &entry) { return !entry->submitted || poll(entry); });
    }
    Ticket register_target(ID3D12Resource *target, UINT64 offset, std::size_t bytes,
                           ID3D12Device *native_device = nullptr) {
        const std::lock_guard lock(mutex_);
        if (!capture_ || !target || !bytes || bytes > ConstantSnapshot::maximum_bytes || entries_.size() >= 6)
            return {};
        D3D12_HEAP_PROPERTIES heap{};
        D3D12_HEAP_FLAGS flags{};
        const auto desc = target->GetDesc();
        if (FAILED(target->GetHeapProperties(&heap, &flags)) || heap.Type != D3D12_HEAP_TYPE_DEFAULT ||
            desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || desc.Width > ConstantSnapshot::maximum_source_bytes ||
            offset > desc.Width || bytes > desc.Width - offset)
            return {};
        for (const auto &entry : entries_)
            if (entry->target.Get() == target && entry->target_offset == offset && entry->bytes == bytes)
                return {}; // Attach only to the originating observed draw.
        auto entry = std::make_shared<Entry>();
        Com<ID3D12Device> parent;
        if (!native_device) {
            if (FAILED(target->GetDevice(IID_PPV_ARGS(&parent))))
                return {};
            native_device = parent.Get();
        }
        entry->device = reinterpret_cast<std::uintptr_t>(native_device);
        entry->target = target;
        entry->target_offset = offset;
        entry->bytes = bytes;
        entry->capture = capture_;
        entry->deadline = Clock::now() + std::chrono::seconds(5);
        entries_.push_back(entry);
        return entry;
    }
    void observe_copy(std::uintptr_t list, ID3D12Resource *source, UINT64 source_offset, ID3D12Resource *destination,
                      UINT64 destination_offset, UINT64 copy_bytes) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_) {
            if (entry->capture != capture_ || entry->matched || entry->submitted || entry->failed ||
                entry->target.Get() != destination)
                continue;
            if (Clock::now() > entry->deadline) {
                entry->state = "NO_UPLOAD_WITHIN_WINDOW";
                continue;
            }
            if (!list || !copy_bytes || copy_bytes == UINT64_MAX || destination_offset > entry->target_offset)
                continue;
            const auto delta = entry->target_offset - destination_offset;
            if (delta > copy_bytes || entry->bytes > copy_bytes - delta || source_offset > UINT64_MAX - delta) {
                entry->state = "NO_COVERING_UPLOAD";
                continue;
            }
            entry->source_info = ConstantSnapshot::inspect_source(source, source_offset + delta, entry->bytes);
            if (!entry->source_info.supported) {
                entry->state = "UPLOAD_SOURCE_INELIGIBLE";
                continue;
            }
            entry->source = source; // Strong reference acquired in valid copy callback.
            entry->source_offset = source_offset + delta;
            entry->list = list;
            entry->matched = true;
            entry->reset_pending = false;
            entry->state = "UPLOAD_MATCHED_NOT_SUBMITTED";
        }
    }
    // Invoke before the application's native execution on its actual queue.
    // The owned list executes and signals its OWN fence here. No completion
    // claim is made about the application list that will execute afterward.
    void before_execute(std::uintptr_t list, ID3D12CommandQueue *queue) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_) {
            if (entry->capture != capture_ || entry->list != list || !entry->matched || entry->submitted ||
                entry->failed)
                continue;
            try {
                entry->copy = std::make_unique<OwnedConstantCopy>();
                if (!entry->copy->submit(queue, entry->source.Get(), entry->source_offset, entry->bytes)) {
                    entry->state = "COPY_NOT_SUBMITTED";
                    entry->failed = true;
                    continue;
                }
                entry->submitted = true;
                entry->state = "GPU_PENDING";
            } catch (...) {
                entry->failed = true;
                entry->state = "COPY_FAILED";
            }
        }
    }
    void before_reset(std::uintptr_t list) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_)
            if (entry->list == list && entry->matched && !entry->submitted)
                entry->reset_pending = true;
    }
    // Call on subsequent recording/Close, after Reset returned, assuming valid
    // D3D12 application use. BEFORE-reset alone does not prove retirement.
    void recording_resumed(std::uintptr_t list, bool destroyed = false) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_) {
            if (entry->list != list || entry->submitted || (!entry->reset_pending && !destroyed))
                continue;
            entry->matched = false;
            entry->source.Reset();
            entry->list = 0;
            entry->reset_pending = false;
            entry->state = "UNSUBMITTED_RECORDING_RETIRED";
        }
    }
    // Call while the parent device is still alive (e.g. primary swapchain
    // destruction), NOT from device finalization. No GetDevice/QI here and no
    // resource Release under our mutex: either can reenter an interop layer.
    void release_device(ID3D12Device *device) {
        if (!device)
            return;
        std::vector<Com<ID3D12Resource>> release;
        release.reserve(12);
        {
            const std::lock_guard lock(mutex_);
            std::erase_if(entries_, [&](const Ticket &entry) {
                if (entry->device != reinterpret_cast<std::uintptr_t>(device))
                    return false;
                (void)poll(entry);
                release.push_back(std::move(entry->source));
                release.push_back(std::move(entry->target));
                entry->matched = false;
                entry->list = 0;
                entry->device_closed = true;
                entry->state = "DEVICE_CLOSED";
                return true;
            });
        }
    }
    Sample read(const Ticket &entry) {
        const std::lock_guard lock(mutex_);
        if (!entry)
            return {};
        if (entry->device_closed)
            return {entry->state, entry->source_info, {}};
        const bool ready = poll(entry);
        if (!entry->matched && !entry->submitted && Clock::now() > entry->deadline)
            entry->state = "NO_UPLOAD_WITHIN_WINDOW";
        return {entry->state, entry->source_info, ready ? *entry->copy->read_completed() : std::vector<std::byte>{}};
    }
};
} // namespace odv::research
