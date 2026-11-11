// SPDX-License-Identifier: MIT
#include "odv/d3d12/stereo_stream.hpp"
#include <array>
#include <cwctype>
#include <limits>
#include <type_traits>
#include <utility>

namespace odv::d3d {
namespace {
constexpr UINT magic = 0x5356444f, version = 1, slot_count = 2;
enum class SlotState : UINT { free, published, reading, faulted };
struct SlotMetadata {
    UINT64 sequence{};
    FrameStamp stamp{};
    SlotState state{};
};
struct SharedHeader {
    UINT signature{}, revision{}, bytes{}, slots{};
    StreamDescription description{};
    DWORD consumer_pid{};
    UINT closed{};
    std::array<SlotMetadata, slot_count> frames{};
};
static_assert(std::is_trivially_copyable_v<SharedHeader>);
struct Handle {
    HANDLE value{};
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
void require(bool value, const char *message) {
    if (!value)
        throw Failure(ErrorCode::configuration, message);
}
std::wstring name(const std::wstring &channel, const std::wstring &suffix) {
    require(!channel.empty() && channel.size() <= 80, "Invalid stereo channel length");
    for (const wchar_t c : channel)
        require((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' ||
                    c == L'-',
                "Invalid stereo channel name");
    return L"Local\\" + channel + suffix;
}
struct Guard {
    HANDLE mutex{};
    bool locked{};
    explicit Guard(HANDLE m, DWORD timeout = 0) : mutex(m) {
        const DWORD result = WaitForSingleObject(m, timeout);
        if (result == WAIT_ABANDONED) {
            ReleaseMutex(m);
            throw Failure(ErrorCode::device, "Stereo metadata owner was lost");
        }
        if (result == WAIT_FAILED)
            throw Failure(ErrorCode::device, "Stereo metadata lock failed");
        locked = result == WAIT_OBJECT_0;
    }
    ~Guard() {
        if (locked)
            ReleaseMutex(mutex);
    }
};
struct Mapping {
    Handle mutex, mapping;
    SharedHeader *header{};
    Mapping(const std::wstring &channel, bool create)
        : mutex(create ? CreateMutexW(nullptr, FALSE, name(channel, L".lock").c_str())
                       : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name(channel, L".lock").c_str())) {
        require(mutex.value != nullptr, "Stereo channel lock unavailable");
        mapping.value =
            create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedHeader),
                                        name(channel, L".header").c_str())
                   : OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name(channel, L".header").c_str());
        const DWORD error = GetLastError();
        require(mapping.value != nullptr && (!create || error != ERROR_ALREADY_EXISTS),
                "Stereo channel mapping unavailable");
        header = static_cast<SharedHeader *>(
            MapViewOfFile(mapping.value, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedHeader)));
        require(header != nullptr, "Stereo channel mapping failed");
    }
    ~Mapping() {
        if (header)
            UnmapViewOfFile(header);
    }
};
void validate(const SharedHeader &h) {
    require(h.signature == magic && h.revision == version && h.bytes == sizeof(SharedHeader) && h.slots == slot_count &&
                !h.closed,
            "Stereo stream unavailable or incompatible");
    require(h.description.width > 0 && h.description.width <= 8192 && h.description.height > 0 &&
                h.description.height <= 8192 && h.description.format == DXGI_FORMAT_R8G8B8A8_UNORM &&
                h.description.producer_pid != 0 &&
                (h.description.color == StreamColor::linear || h.description.color == StreamColor::srgb) &&
                (h.description.origin == StreamOrigin::owned_geometry ||
                 h.description.origin == StreamOrigin::game_candidate),
            "Invalid stereo stream descriptor");
    for (const auto &slot : h.frames)
        require(slot.state == SlotState::free || slot.state == SlotState::published ||
                    slot.state == SlotState::reading || slot.state == SlotState::faulted,
                "Invalid stereo slot state");
}
bool same_adapter(LUID a, LUID b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }
Com<ID3D12Device> queue_device(ID3D12CommandQueue *queue) {
    require(queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT, "Direct stereo queue required");
    Com<ID3D12Device> device;
    hr(queue->GetDevice(IID_PPV_ARGS(&device)), "stereo queue device");
    return device;
}
UINT64 completed(ID3D12Fence *fence) {
    const auto value = fence->GetCompletedValue();
    if (value == UINT64_MAX)
        throw Failure(ErrorCode::device, "Stereo fence device lost");
    return value;
}
bool wait_completed(ID3D12Fence *fence, UINT64 value) {
    if (completed(fence) >= value)
        return true;
    Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    return event.value && SUCCEEDED(fence->SetEventOnCompletion(value, event.value)) &&
           WaitForSingleObject(event.value, 2000) == WAIT_OBJECT_0;
}
void share(ID3D12Device *device, ID3D12DeviceChild *object, const std::wstring &object_name, Handle &handle) {
    hr(device->CreateSharedHandle(object, nullptr, GENERIC_ALL, object_name.c_str(), &handle.value),
       "share stereo object");
}
template <class T> void open_shared(ID3D12Device *device, const std::wstring &object_name, Com<T> &object) {
    Handle handle;
    hr(device->OpenSharedHandleByName(object_name.c_str(), GENERIC_ALL, &handle.value), "open stereo object handle");
    hr(device->OpenSharedHandle(handle.value, IID_PPV_ARGS(&object)), "open stereo object");
}
void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
    if (before == after)
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, 0, before, after};
    list->ResourceBarrier(1, &b);
}
std::wstring eye_name(UINT slot, UINT eye) {
    return L".slot" + std::to_wstring(slot) + (eye == 0 ? L".left" : L".right");
}
} // namespace

struct StereoPublisher::Impl {
    struct Slot {
        std::array<Com<ID3D12Resource>, 2> eyes;
        std::array<Com<ID3D12Resource>, 2> sources;
        std::array<Handle, 2> handles;
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> list;
        UINT64 last_copy{};
    };
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    Com<ID3D12Fence> ready, released;
    Handle ready_handle, released_handle;
    std::wstring channel;
    std::unique_ptr<Mapping> mapping;
    std::array<Slot, slot_count> slots;
    StreamDescription description{};
    UINT64 sequence{}, drops{};
    bool faulted{};
    Impl(ID3D12CommandQueue *q, UINT width, UINT height, StreamOrigin origin, StreamColor color)
        : device(queue_device(q)), queue(q) {
        require(width && width <= 8192 && height && height <= 8192, "Invalid stereo dimensions");
        require(origin == StreamOrigin::owned_geometry || origin == StreamOrigin::game_candidate,
                "Invalid stereo origin");
        require(color == StreamColor::linear || color == StreamColor::srgb, "Invalid stereo color encoding");
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        channel =
            L"OpenDesertVR.Stereo." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(counter.QuadPart);
        mapping = std::make_unique<Mapping>(channel, true);
        hr(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&ready)), "stereo ready fence");
        hr(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&released)), "stereo release fence");
        share(device.Get(), ready.Get(), name(channel, L".ready"), ready_handle);
        share(device.Get(), released.Get(), name(channel, L".released"), released_handle);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        for (UINT i = 0; i < slot_count; ++i) {
            auto &slot = slots[i];
            for (UINT eye = 0; eye < 2; ++eye) {
                hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                   nullptr, IID_PPV_ARGS(&slot.eyes[eye])),
                   "shared eye texture");
                share(device.Get(), slot.eyes[eye].Get(), name(channel, eye_name(i, eye)), slot.handles[eye]);
            }
            hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator)),
               "stereo copy allocator");
            hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&slot.list)),
               "stereo copy list");
            hr(slot.list->Close(), "close stereo copy list");
        }
        Guard lock(mapping->mutex.value, 1000);
        require(lock.locked, "Stereo initialization lock unavailable");
        auto &h = *mapping->header;
        h = {};
        description = {width, height, desc.Format, device->GetAdapterLuid(), GetCurrentProcessId(), origin, color};
        h.description = description;
        h.revision = version;
        h.bytes = sizeof(SharedHeader);
        h.slots = slot_count;
        h.signature = magic; // Publish initialized objects last under the metadata lock.
    }
    ~Impl() {
        try {
            Guard lock(mapping->mutex.value, 100);
            if (lock.locked)
                mapping->header->closed = 1;
        } catch (...) {
        }
        bool safe = !faulted;
        try {
            if (sequence)
                safe = safe && wait_completed(ready.Get(), sequence);
        } catch (...) {
            safe = false;
        }
        if (!safe) {
            // Unknown copy completion: retain GPU references until process exit.
            // No game queue wait or early reuse is introduced by teardown.
            for (auto &slot : slots) {
                for (auto &eye : slot.eyes)
                    eye.Detach();
                for (auto &source : slot.sources)
                    source.Detach();
                slot.list.Detach();
                slot.allocator.Detach();
            }
            ready.Detach();
            released.Detach();
            queue.Detach();
            device.Detach();
        }
    }
};
StereoPublisher::StereoPublisher(ID3D12CommandQueue *q, UINT w, UINT h, StreamOrigin origin, StreamColor color)
    : impl_(std::make_unique<Impl>(q, w, h, origin, color)) {}
StereoPublisher::~StereoPublisher() = default;
const std::wstring &StereoPublisher::channel() const { return impl_->channel; }
UINT64 StereoPublisher::dropped_pairs() const { return impl_->drops; }
PublishResult StereoPublisher::publish(ID3D12Resource *left, ID3D12Resource *right, D3D12_RESOURCE_STATES left_state,
                                       D3D12_RESOURCE_STATES right_state, FrameStamp stamp) {
    auto &p = *impl_;
    require(!p.faulted && left && right && left != right && stamp.left_source_frame &&
                stamp.right_source_frame >= stamp.left_source_frame &&
                stamp.right_source_frame - stamp.left_source_frame <= 1,
            "Invalid stereo image pair");
    const auto &description = p.description;
    for (auto *source : {left, right}) {
        const auto d = source->GetDesc();
        Com<ID3D12Device> owner;
        hr(source->GetDevice(IID_PPV_ARGS(&owner)), "stereo source device");
        require(owner.Get() == p.device.Get() && d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
                    d.Width == description.width && d.Height == description.height && d.Format == description.format &&
                    d.DepthOrArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1,
                "Stereo source descriptor/device mismatch");
    }
    Guard lock(p.mapping->mutex.value);
    if (!lock.locked) {
        ++p.drops;
        return PublishResult::busy;
    }
    auto &header = *p.mapping->header;
    validate(header);
    require(header.description.width == description.width && header.description.height == description.height &&
                header.description.format == description.format &&
                same_adapter(header.description.adapter, description.adapter),
            "Stereo descriptor changed");
    const auto ready = completed(p.ready.Get()), released = completed(p.released.Get());
    UINT selected = slot_count;
    for (UINT i = 0; i < slot_count; ++i) {
        const auto &metadata = header.frames[i];
        const bool available = metadata.state == SlotState::free ||
                               (metadata.state == SlotState::published && ready >= metadata.sequence) ||
                               (metadata.state == SlotState::reading && released >= metadata.sequence);
        if (available && ready >= p.slots[i].last_copy) {
            selected = i;
            break;
        }
    }
    if (selected == slot_count) {
        ++p.drops;
        return PublishResult::busy;
    }
    require(p.sequence < UINT64_MAX - 1, "Stereo sequence exhausted");
    auto &slot = p.slots[selected];
    auto &metadata = header.frames[selected];
    try {
        // A release wait is issued only after CPU proof that it is completed;
        // it provides cross-queue ordering without stalling on the receiver.
        if (metadata.state == SlotState::reading)
            hr(p.queue->Wait(p.released.Get(), metadata.sequence), "order released stereo slot");
        hr(slot.allocator->Reset(), "reset stereo allocator");
        hr(slot.list->Reset(slot.allocator.Get(), nullptr), "reset stereo list");
        const std::array<ID3D12Resource *, 2> inputs{left, right};
        slot.sources = {left, right}; // Retain sources through the asynchronous copies.
        const std::array<D3D12_RESOURCE_STATES, 2> states{left_state, right_state};
        for (UINT eye = 0; eye < 2; ++eye) {
            transition(slot.list.Get(), inputs[eye], states[eye], D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(slot.list.Get(), slot.eyes[eye].Get(), D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_COPY_DEST);
            slot.list->CopyResource(slot.eyes[eye].Get(), inputs[eye]);
            transition(slot.list.Get(), slot.eyes[eye].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_COMMON);
            transition(slot.list.Get(), inputs[eye], D3D12_RESOURCE_STATE_COPY_SOURCE, states[eye]);
        }
        hr(slot.list->Close(), "close stereo list");
        metadata.state = SlotState::faulted; // Remains unusable if submission/signal fails.
        ID3D12CommandList *lists[]{slot.list.Get()};
        p.queue->ExecuteCommandLists(1, lists);
        const auto sequence = ++p.sequence;
        hr(p.queue->Signal(p.ready.Get(), sequence), "signal stereo copy completion");
        slot.last_copy = sequence;
        metadata = {sequence, stamp, SlotState::published};
    } catch (...) {
        p.faulted = true;
        throw;
    }
    return PublishResult::published;
}

StreamDescription inspect_stereo_stream(const std::wstring &channel) {
    Mapping mapping(channel, false);
    Guard lock(mapping.mutex.value, 1000);
    require(lock.locked, "Stereo descriptor lock unavailable");
    validate(*mapping.header);
    return mapping.header->description;
}
struct StereoReceiver::Impl {
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    Com<ID3D12Fence> ready, released;
    std::array<std::array<Com<ID3D12Resource>, 2>, slot_count> eyes;
    Mapping mapping;
    Handle producer_process;
    StreamDescription description;
    UINT64 last_sequence{}, active_sequence{}, last_release{};
    bool registered{};
    Impl(ID3D12CommandQueue *q, const std::wstring &channel)
        : device(queue_device(q)), queue(q), mapping(channel, false) {
        Guard lock(mapping.mutex.value, 1000);
        require(lock.locked, "Stereo receiver lock unavailable");
        validate(*mapping.header);
        description = mapping.header->description;
        producer_process.value = OpenProcess(SYNCHRONIZE, FALSE, description.producer_pid);
        require(producer_process.value && WaitForSingleObject(producer_process.value, 0) == WAIT_TIMEOUT,
                "Stereo producer is unavailable");
        require(same_adapter(device->GetAdapterLuid(), description.adapter), "Stereo adapter mismatch");
        require(mapping.header->consumer_pid == 0, "Stereo stream already has a receiver");
        open_shared(device.Get(), name(channel, L".ready"), ready);
        open_shared(device.Get(), name(channel, L".released"), released);
        for (UINT i = 0; i < slot_count; ++i)
            for (UINT eye = 0; eye < 2; ++eye) {
                open_shared(device.Get(), name(channel, eye_name(i, eye)), eyes[i][eye]);
                const auto d = eyes[i][eye]->GetDesc();
                require(d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width == description.width &&
                            d.Height == description.height && d.Format == description.format &&
                            d.DepthOrArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1,
                        "Shared stereo texture descriptor mismatch");
            }
        mapping.header->consumer_pid = GetCurrentProcessId();
        registered = true;
    }
    ~Impl() {
        bool safe = false;
        try {
            if (active_sequence) {
                hr(queue->Signal(released.Get(), active_sequence), "retire stereo lease");
                last_release = active_sequence;
            }
            safe = !last_release || wait_completed(released.Get(), last_release);
            Guard lock(mapping.mutex.value, 100);
            if (safe && registered && lock.locked)
                mapping.header->consumer_pid = 0;
        } catch (...) {
        }
        if (!safe) {
            for (auto &slot : eyes)
                for (auto &eye : slot)
                    eye.Detach();
            ready.Detach();
            released.Detach();
            queue.Detach();
            device.Detach();
        }
    }
};
StereoReceiver::StereoReceiver(ID3D12CommandQueue *q, const std::wstring &channel)
    : impl_(std::make_unique<Impl>(q, channel)) {}
StereoReceiver::~StereoReceiver() = default;
const StreamDescription &StereoReceiver::description() const { return impl_->description; }
std::optional<StereoReadFrame> StereoReceiver::acquire() {
    auto &p = *impl_;
    require(WaitForSingleObject(p.producer_process.value, 0) == WAIT_TIMEOUT, "Stereo producer disconnected");
    require(!p.active_sequence, "Release the previous stereo lease first");
    Guard lock(p.mapping.mutex.value);
    if (!lock.locked)
        return {};
    validate(*p.mapping.header);
    const auto done = completed(p.ready.Get());
    UINT selected = slot_count;
    UINT64 newest = (std::max)(p.last_sequence, completed(p.released.Get()));
    for (UINT i = 0; i < slot_count; ++i) {
        const auto &m = p.mapping.header->frames[i];
        if (m.state == SlotState::published && m.sequence > newest && done >= m.sequence) {
            require(m.stamp.left_source_frame && m.stamp.right_source_frame >= m.stamp.left_source_frame &&
                        m.stamp.right_source_frame - m.stamp.left_source_frame <= 1,
                    "Invalid shared frame stamp");
            selected = i;
            newest = m.sequence;
        }
    }
    if (selected == slot_count)
        return {};
    hr(p.queue->Wait(p.ready.Get(), newest), "order received stereo pair");
    p.mapping.header->frames[selected].state = SlotState::reading;
    p.last_sequence = p.active_sequence = newest;
    return StereoReadFrame{p.eyes[selected][0].Get(), p.eyes[selected][1].Get(),
                           p.mapping.header->frames[selected].stamp, newest};
}
void StereoReceiver::release() {
    auto &p = *impl_;
    require(p.active_sequence != 0, "No stereo lease to release");
    hr(p.queue->Signal(p.released.Get(), p.active_sequence), "release stereo pair");
    p.last_release = p.active_sequence;
    p.active_sequence = 0;
}
bool StereoReceiver::newer_available() const {
    auto &p = *impl_;
    require(WaitForSingleObject(p.producer_process.value, 0) == WAIT_TIMEOUT, "Stereo producer disconnected");
    Guard lock(p.mapping.mutex.value);
    if (!lock.locked)
        return false;
    validate(*p.mapping.header);
    const auto done = completed(p.ready.Get());
    for (const auto &slot : p.mapping.header->frames)
        if (slot.state == SlotState::published && slot.sequence > p.last_sequence && done >= slot.sequence)
            return true;
    return false;
}
} // namespace odv::d3d
