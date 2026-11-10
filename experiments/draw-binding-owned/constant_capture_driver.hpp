// SPDX-License-Identifier: MIT
#pragma once
#include "constant_snapshot.hpp"
#include "vertex_constant_stream.hpp"
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
        std::unique_ptr<VertexConstantStream> vertex;
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
                return entry->vertex ? entry->vertex->discard_unsubmitted() : entry->snapshot.discard_unsubmitted();
            if (entry->pending || entry->ambiguous)
                return false;
            // Preserve an immutable CPU sample before retiring its allocation.
            try {
                if (entry->vertex) {
                    (void)entry->vertex->read();
                    return entry->vertex->release_retired();
                }
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
        return record_impl(list, device, commands, source, offset, bytes, nullptr, status);
    }
    Ticket record_default(std::uintptr_t list, ID3D12Device *device, ID3D12GraphicsCommandList *commands,
                          ID3D12Resource *source, UINT64 offset, std::size_t bytes, const KnownBufferStates &states,
                          RecordStatus *status = nullptr) {
        return record_impl(list, device, commands, source, offset, bytes, &states, status);
    }
    template <class Forward>
    Ticket record_work(std::uintptr_t list, std::uintptr_t identity, ID3D12Device *device,
                       ID3D12GraphicsCommandList *commands, D3D12_QUERY_TYPE type, Forward &&forward) {
        const std::lock_guard lock(mutex_);
        collect_retired();
        if (!list || !identity || !capture_ || entries_.size() >= ConstantSnapshot::maximum_pending)
            return {};
        // Identity is the caller's immutable draw variant, not a resource to read.
        for (const auto &entry : entries_)
            if (!entry->retired && entry->list == list && entry->source == identity && entry->bytes == 0)
                return {};
        auto entry = std::make_shared<Entry>();
        entry->list = list;
        entry->source = identity;
        entry->capture = capture_;
        entries_.push_back(entry);
        try {
            if (!entry->snapshot.record_work(device, commands, type, std::forward<Forward>(forward))) {
                entries_.pop_back();
                return {};
            }
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        return entry;
    }
    template <class Restore>
    Ticket record_vertex(std::uintptr_t list, ID3D12Device *device, ID3D12GraphicsCommandList *commands,
                         ID3D12Resource *source, UINT64 offset, const VertexConstantStream::Context &context,
                         Restore &&restore, RecordStatus *status = nullptr) {
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
        if (entries_.size() >= VertexConstantStream::maximum_pending)
            return reject("CAPACITY_REACHED");
        result.source = ConstantSnapshot::inspect_source(source, offset, VertexConstantStream::byte_count);
        const auto identity = reinterpret_cast<std::uintptr_t>(source);
        for (const auto &entry : entries_)
            if (!entry->retired && entry->list == list && entry->source == identity && entry->offset == offset &&
                entry->bytes == VertexConstantStream::byte_count)
                return reject("DUPLICATE_SOURCE_RANGE");
        auto entry = std::make_shared<Entry>();
        entry->vertex = std::make_unique<VertexConstantStream>();
        entry->list = list;
        entry->source = identity;
        entry->offset = offset;
        entry->bytes = VertexConstantStream::byte_count;
        entry->capture = capture_;
        entries_.push_back(entry); // Tracking allocation precedes GPU recording.
        try {
            if (!entry->vertex->record(device, commands, source, offset, context, std::forward<Restore>(restore))) {
                entries_.pop_back();
                return reject("VERTEX_READ_NOT_RECORDED");
            }
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        result.source.state = "ELIGIBLE_ACTIVE_VERTEX_CBV";
        result.source.supported = true;
        result.state = "RECORDED_VERTEX_STREAM";
        if (status)
            *status = result;
        return entry;
    }

  private:
    Ticket record_impl(std::uintptr_t list, ID3D12Device *device, ID3D12GraphicsCommandList *commands,
                       ID3D12Resource *source, UINT64 offset, std::size_t bytes, const KnownBufferStates *states,
                       RecordStatus *status) {
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
        result.source = states ? ConstantSnapshot::inspect_default_source(source, offset, bytes, *states)
                               : ConstantSnapshot::inspect_source(source, offset, bytes);
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
            const bool recorded = states
                                      ? entry->snapshot.record_default(device, commands, source, offset, bytes, *states)
                                      : entry->snapshot.record(device, commands, source, offset, bytes);
            if (!recorded) {
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

  public:
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
            if (entry->vertex)
                (void)entry->vertex->before_execute();
            else
                (void)entry->snapshot.before_execute();
        }
    }
    void after_present(ID3D12CommandQueue *queue) { after_submission(queue, std::nullopt); }
    // Call only from an actual AFTER-native-ExecuteCommandLists callback for
    // this recording. Present timing and a later frame are not prerequisites.
    void after_execute(std::uintptr_t list, ID3D12CommandQueue *queue) {
        if (list)
            after_submission(queue, list);
    }

  private:
    void after_submission(ID3D12CommandQueue *queue, std::optional<std::uintptr_t> list) {
        const std::lock_guard lock(mutex_);
        for (const auto &entry : entries_) {
            if (!queue || !entry->pending || entry->ambiguous || entry->failed || entry->queue.Get() != queue ||
                entry->thread != std::this_thread::get_id() || (list && entry->list != *list) ||
                (entry->vertex && !list)) // Shader reads require exact native after-execute, never Present.
                continue;
            Com<ID3D12Device> device;
            Com<ID3D12Fence> fence;
            if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
                FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
                FAILED(queue->Signal(fence.Get(), 1))) {
                entry->failed = true;
                continue;
            }
            // Caller has returned from native execution submission, either via
            // the exact callback or the qualified same-thread present route.
            const bool submitted = entry->vertex ? entry->vertex->submitted_after_execute(fence.Get(), 1)
                                                 : entry->snapshot.submitted_after_execute(fence.Get(), 1);
            if (!submitted) {
                entry->failed = true;
                continue;
            }
            entry->pending = false;
        }
        collect_retired();
    }

  public:
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
            if (entry->vertex) {
                auto result = entry->vertex->read();
                return {result.state, std::move(result.bytes)};
            }
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
