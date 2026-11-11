// SPDX-License-Identifier: MIT
#include "odv/d3d12/stereo_stream.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace odv;
using namespace odv::d3d;
namespace {
constexpr UINT width = 128, height = 128;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F &&f) {
    bool rejected = false;
    try {
        f();
    } catch (const Failure &) {
        rejected = true;
    }
    require(rejected, "invalid stereo operation accepted");
}
LUID warp_adapter() {
    Com<IDXGIFactory4> factory;
    hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "stream test factory");
    Com<IDXGIAdapter1> adapter;
    hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "stream test WARP");
    DXGI_ADAPTER_DESC1 desc{};
    hr(adapter->GetDesc1(&desc), "stream test adapter");
    return desc.AdapterLuid;
}
Com<ID3D12Resource> image(Renderer &g, double eye) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = width;
    d.Height = height;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Com<ID3D12Resource> result;
    hr(g.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                           IID_PPV_ARGS(&result)),
       "stream test eye");
    g.render(result.Get(), d.Format, width, height,
             stereo::vertices(math::eye_pose({}, eye), {-0.7, 0.7, 0.7, -0.7}, .05, 100));
    return result;
}
std::vector<std::byte> pixels(Renderer &g, ID3D12Resource *source, D3D12_RESOURCE_STATES state) {
    auto desc = source->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes{};
    g.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Com<ID3D12Resource> readback;
    hr(g.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                           IID_PPV_ARGS(&readback)),
       "stream test readback");
    Com<ID3D12CommandAllocator> allocator;
    Com<ID3D12GraphicsCommandList> list;
    hr(g.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "read allocator");
    hr(g.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
       "read list");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {source, 0, state, D3D12_RESOURCE_STATE_COPY_SOURCE};
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE)
        list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = source;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = readback.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = footprint;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
    }
    hr(list->Close(), "close stream read list");
    ID3D12CommandList *lists[]{list.Get()};
    g.queue()->ExecuteCommandLists(1, lists);
    g.sync();
    void *mapped{};
    const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    hr(readback->Map(0, &range, &mapped), "map stream pixels");
    std::vector<std::byte> result(width * height * 4);
    for (UINT y = 0; y < height; ++y)
        memcpy(result.data() + y * width * 4,
               static_cast<std::byte *>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch, width * 4);
    const D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    return result;
}
StereoReadFrame acquire(StereoReceiver &receiver) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end) {
        if (auto frame = receiver.acquire())
            return *frame;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("stereo pair did not become available");
}
void check_pair(Renderer &g, const StereoReadFrame &f, const std::vector<std::byte> &left,
                const std::vector<std::byte> &right) {
    require(f.stamp.left_source_frame == 7 && f.stamp.right_source_frame == 8, "eye source serial mismatch");
    require(pixels(g, f.left, D3D12_RESOURCE_STATE_COMMON) == left, "left eye bytes changed");
    require(pixels(g, f.right, D3D12_RESOURCE_STATE_COMMON) == right, "right eye bytes changed");
}
void run_child(const std::wstring &channel) {
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "test executable path unavailable");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --consume " + channel;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process) != FALSE,
            "stream test child launch failed");
    const DWORD waited = WaitForSingleObject(process.hProcess, 15000);
    if (waited != WAIT_OBJECT_0) {
        // This handle is exclusively the owned test child created above.
        TerminateProcess(process.hProcess, 2);
        WaitForSingleObject(process.hProcess, 2000);
    }
    DWORD exit_code{};
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    require(waited == WAIT_OBJECT_0 && exit_code == 0, "cross-process eye consumer failed");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--serve") {
            Renderer graphics;
            graphics.initialize(warp_adapter(), D3D_FEATURE_LEVEL_11_0, false);
            graphics.pipeline(DXGI_FORMAT_R8G8B8A8_UNORM);
            auto l = image(graphics, -.032), r = image(graphics, .032);
            StereoPublisher publisher(graphics.queue(), width, height, StreamOrigin::owned_geometry);
            require(publisher.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET, {1, 1}) == PublishResult::published,
                    "initial fixture pair failed");
            graphics.sync();
            std::ofstream channel_file{std::filesystem::path(argv[2])};
            channel_file.exceptions(std::ios::badbit | std::ios::failbit);
            for (const wchar_t c : publisher.channel())
                channel_file << static_cast<char>(c);
            channel_file << '\n';
            channel_file.close();
            const auto start = std::chrono::steady_clock::now();
            UINT64 frames = 1;
            while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
                const auto t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                const double offset = .05 * std::sin(t * 2);
                graphics.render(l.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
                                stereo::vertices(math::eye_pose({}, offset - .032), {-.7, .7, .7, -.7}, .05, 100));
                graphics.render(r.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
                                stereo::vertices(math::eye_pose({}, offset + .032), {-.7, .7, .7, -.7}, .05, 100));
                ++frames;
                publisher.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, {frames, frames});
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::cout << "PASS: owned stereo source frames=" << frames << "; game=NOT_RUN\n";
            return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"--consume") {
            const auto description = inspect_stereo_stream(argv[2]);
            require(description.width == width && description.height == height &&
                        description.origin == StreamOrigin::owned_geometry,
                    "child stream descriptor mismatch");
            Renderer graphics;
            graphics.initialize(description.adapter, D3D_FEATURE_LEVEL_11_0, false);
            graphics.pipeline(DXGI_FORMAT_R8G8B8A8_UNORM);
            auto l = image(graphics, -.032), r = image(graphics, .032);
            const auto left = pixels(graphics, l.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET),
                       right = pixels(graphics, r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            require(left != right, "owned baseline is not stereo");
            StereoReceiver receiver(graphics.queue(), argv[2]);
            check_pair(graphics, acquire(receiver), left, right);
            receiver.release();
            graphics.sync();
            std::cout << "PASS: cross-process exact independent-eye pixels; game=NOT_RUN\n";
            return 0;
        }
        const bool debug = argc == 2 && std::wstring_view(argv[1]) == L"--debug-layer";
        require(argc == 1 || debug, "invalid stream test arguments");
        if (debug) {
            Com<ID3D12Debug> layer;
            if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)))) {
                std::cout << "D3D12 debug layer unavailable; stream validation NOT_RUN\n";
                return 77;
            }
            layer->EnableDebugLayer();
        }
        Renderer producer, consumer;
        const auto luid = warp_adapter();
        producer.initialize(luid, D3D_FEATURE_LEVEL_11_0, false);
        producer.pipeline(DXGI_FORMAT_R8G8B8A8_UNORM);
        consumer.initialize(luid, D3D_FEATURE_LEVEL_11_0, false);
        consumer.pipeline(DXGI_FORMAT_R8G8B8A8_UNORM);
        auto l = image(producer, -.032), r = image(producer, .032);
        const auto left = pixels(producer, l.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET),
                   right = pixels(producer, r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        require(left != right, "owned stereo images are identical");
        StereoPublisher publisher(producer.queue(), width, height, StreamOrigin::owned_geometry);
        {
            StereoReceiver receiver(consumer.queue(), publisher.channel());
            require(!receiver.acquire(), "empty channel exposed a frame");
            rejects([&] { StereoReceiver duplicate(consumer.queue(), publisher.channel()); });
            rejects([&] {
                publisher.publish(l.Get(), l.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, {7, 8});
            });
            rejects([&] {
                publisher.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, {7, 9});
            });
            require(publisher.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET, {7, 8}) == PublishResult::published,
                    "first pair was not published");
            const auto held = acquire(receiver);
            require(!receiver.newer_available(), "held frame was reported as newer");
            rejects([&] { receiver.acquire(); });
            for (int i = 0; i < 4; ++i) {
                require(publisher.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                          D3D12_RESOURCE_STATE_RENDER_TARGET, {7, 8}) == PublishResult::published,
                        "unleased slot failed to advance");
                producer.sync();
            }
            check_pair(consumer, held, left, right); // Retained pair survived newer publication.
            require(receiver.newer_available(), "newer published pair was not detected");
            auto output = image(consumer, 0);
            consumer.render_texture(output.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, width, height, held.left);
            require(pixels(consumer, output.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET) == left,
                    "left stream texture rendering changed pixels");
            consumer.render_texture(output.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, width, height, held.right);
            require(pixels(consumer, output.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET) == right,
                    "right stream texture rendering changed pixels");
            receiver.release();
            check_pair(consumer, acquire(receiver), left, right);
            receiver.release();
            consumer.sync();
            require(!receiver.acquire(), "stale pair was reacquired");
        }
        require(publisher.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, {7, 8}) == PublishResult::published,
                "reconnected pair was not published");
        producer.sync();
        run_child(publisher.channel());
        {
            StereoPublisher blocked(producer.queue(), width, height, StreamOrigin::owned_geometry);
            StereoReceiver reader(consumer.queue(), blocked.channel());
            Com<ID3D12Fence> gate;
            hr(producer.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "test queue gate");
            struct OpenGate {
                ID3D12Fence *fence;
                ~OpenGate() { fence->Signal(1); }
            } cleanup{gate.Get()};
            hr(producer.queue()->Wait(gate.Get(), 1), "hold owned test queue");
            for (int i = 0; i < 2; ++i)
                require(blocked.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, {7, 8}) == PublishResult::published,
                        "pending slot was not recorded");
            require(!reader.acquire(), "incomplete GPU pair was exposed");
            require(blocked.publish(l.Get(), r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                    D3D12_RESOURCE_STATE_RENDER_TARGET, {7, 8}) == PublishResult::busy &&
                        blocked.dropped_pairs() == 1,
                    "busy transport did not drop the pair");
            hr(gate->Signal(1), "release owned test queue");
            producer.sync();
            check_pair(consumer, acquire(reader), left, right);
            reader.release();
            consumer.sync();
        }
        require(pixels(producer, l.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET) == left &&
                    pixels(producer, r.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET) == right,
                "original producer images changed");
        rejects([&] { inspect_stereo_stream(L"invalid\\channel"); });
        if (debug) {
            for (auto *device : {producer.device(), consumer.device()}) {
                Com<ID3D12InfoQueue> messages;
                hr(device->QueryInterface(IID_PPV_ARGS(&messages)), "stream validation queue");
                for (UINT64 i = 0; i < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                    SIZE_T size{};
                    hr(messages->GetMessage(i, nullptr, &size), "stream validation message size");
                    std::vector<unsigned char> bytes(size);
                    auto message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
                    hr(messages->GetMessage(i, message, &size), "stream validation message");
                    require(message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION &&
                                message->Severity != D3D12_MESSAGE_SEVERITY_ERROR,
                            message->pDescription);
                }
            }
            std::cout << "D3D12 stream validation: no corruption or error messages\n";
        }
        std::cout << "PASS: same-adapter stereo transport, two-device/cross-process exact pixels, lease retention, "
                     "reconnect, source restoration and invalid-pair rejection; game=NOT_RUN; headset=NOT_RUN\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
