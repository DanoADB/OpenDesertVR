// SPDX-License-Identifier: MIT
#pragma once
#include "constant_snapshot.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

namespace odv::research {
// Native bridge for a BEFORE-execute callback and a later same-thread,
// same-queue AFTER-present callback. Never signal a fence in before_execute.
// Call every execution/reset/destroy event, even outside an active capture.
// Valid D3D12 application ordering is required; this is not fault isolation.
class ConstantCaptureDriver final {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;

  public:
    struct Entry {
      private:
        friend class ConstantCaptureDriver;
        ConstantSnapshot snapshot;
        std::uintptr_t list = 0, source = 0;
        UINT64 offset = 0, capture = 0;
        std::size_t bytes = 0;
        Com<ID3D12CommandQueue> queue;
        std::thread::id thread;
        bool executed = false, pending = false, ambiguous = false;
        bool reset_pending = false, retired = false, failed = false;
    };
    using Ticket = std::shared_ptr<Entry>;
    struct Sample {
        const char *state = "GPU_PENDING";
        std::vector<std::byte> bytes;
    };
    struct RecordStatus {
        const char *state = "INVALID_CAPTURE_CONTEXT";
        ConstantSnapshot::SourceInfo source;
    };

  private:
    std::mutex mutex_;
    std::vector<Ticket> entries_;
    UINT64 capture_ = 0;
    void collect_retired() {
        std::erase_if(entries_, [](const Ticket &entry) {
            if (!entry->retired || entry->failed)
                return false;
            if (!entry->executed)
                return entry->snapshot.discard_unsubmitted();
            if (entry->pending || entry->ambiguous)
                return false;
            // Preserve an immutable CPU sample before retiring its allocation.
            try {
                (void)entry->snapshot.read_completed();
                return entry->snapshot.release_retired();
            } catch (...) {
                entry->failed = true;
                return false;
            }
        });
    }

  public:
    void begin_capture() {
        const std::lock_guard lock(mutex_);
        ++capture_;
        collect_retired();
    }
    Ticket record(std::uintptr_t list, ID3D12Device *device, ID3D12GraphicsCommandList *commands,
                  ID3D12Resource *source, UINT64 offset, std::size_t bytes, RecordStatus *status = nullptr) {
        const std::lock_guard lock(mutex_);
        RecordStatus result;
        const auto reject = [&](const char *reason) -> Ticket {
            result.state = reason;
            if (status)
                *status = result;
            return {};
        };
        collect_retired();
        if (!list || !capture_ || !device || !commands)
            return reject("INVALID_CAPTURE_CONTEXT");
        if (entries_.size() >= ConstantSnapshot::maximum_pending)
            return reject("CAPACITY_REACHED");
        result.source = ConstantSnapshot::inspect_source(source, offset, bytes);
        if (!result.source.supported)
            return reject("INELIGIBLE_SOURCE");
        const auto identity = reinterpret_cast<std::uintptr_t>(source);
        // Attach only to the draw that actually recorded this copy. Reusing a
        // ticket on a later draw could mislabel different constant contents.
        for (const auto &entry : entries_)
            if (!entry->retired && entry->list == list && entry->source == identity && entry->offset == offset &&
                entry->bytes == bytes)
                return reject("DUPLICATE_SOURCE_RANGE");
        auto entry = std::make_shared<Entry>();
        entry->list = list;
        entry->source = identity;
        entry->offset = offset;
        entry->bytes = bytes;
        entry->capture = capture_;
        // Allocate tracking storage BEFORE inserting a native GPU command.
        entries_.push_back(entry);
        try {
            if (!entry->snapshot.record(device, commands, source, offset, bytes)) {
                entries_.pop_back();
                return reject("COPY_NOT_RECORDED");
            }
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        result.state = "RECORDED";
        if (status)
            *status = result;
        return entry;
    }
    void before_execute(std::uintptr_t list, ID3D12CommandQueue *queue) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_) {
            if (entry->list != list || entry->retired)
                continue;
            const auto thread = std::this_thread::get_id();
            if (!queue || (entry->executed && (entry->queue.Get() != queue || entry->thread != thread)))
                entry->ambiguous = true;
            if (!entry->executed) {
                entry->queue = queue;
                entry->thread = thread;
            }
            entry->executed = true;
            entry->pending = true;
            (void)entry->snapshot.before_execute();
        }
    }
    void after_present(ID3D12CommandQueue *queue) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_) {
            if (!queue || !entry->pending || entry->ambiguous || entry->failed || entry->queue.Get() != queue ||
                entry->thread != std::this_thread::get_id())
                continue;
            Com<ID3D12Device> device;
            Com<ID3D12Fence> fence;
            if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
                FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
                FAILED(queue->Signal(fence.Get(), 1))) {
                entry->failed = true;
                continue;
            }
            // Same CPU thread reached AFTER-present after returning from its
            // earlier native ExecuteCommandLists. Signal follows the copy.
            if (!entry->snapshot.submitted_after_execute(fence.Get(), 1)) {
                entry->failed = true;
                continue;
            }
            entry->pending = false;
        }
        collect_retired();
    }
    void before_reset(std::uintptr_t list) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_)
            if (entry->list == list && !entry->retired)
                entry->reset_pending = true;
    }
    // Called from a subsequent recording callback after native Reset returned.
    // In a conforming app, new commands imply a successful reset. The BEFORE-
    // reset callback by itself never retires or frees the old recording.
    void recording_resumed(std::uintptr_t list) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_)
            if (entry->list == list && entry->reset_pending)
                entry->retired = true;
        collect_retired();
    }
    void destroyed(std::uintptr_t list) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_)
            if (entry->list == list)
                entry->retired = true;
        collect_retired();
    }
    Sample read(const Ticket &entry) {
        const std::lock_guard lock(mutex_);
        if (!entry)
            return {"NOT_CAPTURED", {}};
        if (entry->failed || entry->ambiguous)
            return {"UNVERIFIED_SUBMISSION", {}};
        try {
            const auto &bytes = entry->snapshot.read_completed();
            if (bytes)
                return {"READY", *bytes};
        } catch (...) {
            entry->failed = true;
            return {"READ_FAILED", {}};
        }
        return {entry->executed ? "GPU_PENDING" : "UNSUBMITTED", {}};
    }
};
} // namespace odv::research
