// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <chrono>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <windows.h>
#include <wrl/client.h>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
void checked(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("Owned presenting fixture graphics operation failed");
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Provide a local capture prefix");
        const auto stop = std::filesystem::path(argv[1]).parent_path() / "stop.flag";
        WNDCLASSW wc{};
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"OpenDesertVRPresentFixture";
        if (!RegisterClassW(&wc))
            throw std::runtime_error("Could not register owned fixture window");
        const auto destroy_window = [](HWND value) { DestroyWindow(value); };
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(destroy_window)> window(
            CreateWindowW(wc.lpszClassName, L"OpenDesertVR owned capture fixture", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, 320, 180, nullptr, nullptr, wc.hInstance, nullptr),
            destroy_window);
        if (!window)
            throw std::runtime_error("Could not create owned fixture window");
        Com<IDXGIFactory4> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.Width = 320;
        swap_desc.Height = 180;
        swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = 2;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Com<IDXGISwapChain1> initial_swap;
        checked(
            factory->CreateSwapChainForHwnd(queue.Get(), window.get(), &swap_desc, nullptr, nullptr, &initial_swap));
        Com<IDXGISwapChain3> swap;
        checked(initial_swap.As(&swap));
        checked(factory->MakeWindowAssociation(window.get(), DXGI_MWA_NO_ALT_ENTER));
        Com<ID3D12Resource> buffers[2];
        D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
        rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_desc.NumDescriptors = 3;
        Com<ID3D12DescriptorHeap> rtv;
        checked(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv)));
        const auto base = rtv->GetCPUDescriptorHandleForHeapStart();
        const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < 2; ++i) {
            checked(swap->GetBuffer(i, IID_PPV_ARGS(&buffers[i])));
            device->CreateRenderTargetView(buffers[i].Get(), nullptr, {base.ptr + i * stride});
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 16;
        desc.Height = 16;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Com<ID3D12Resource> target;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                nullptr, IID_PPV_ARGS(&target)));
        const D3D12_CPU_DESCRIPTOR_HANDLE target_rtv{base.ptr + 2 * stride};
        device->CreateRenderTargetView(target.Get(), nullptr, target_rtv);
        Com<ID3D12CommandAllocator> allocator;
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList> commands;
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&commands)));
        checked(commands->Close());
        Com<ID3D12Fence> fence;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        const std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr),
                                                                  CloseHandle);
        if (!event)
            throw std::runtime_error("Could not create fence event");
        ShowWindow(window.get(), SW_SHOWNOACTIVATE);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        UINT64 frame = 0;
        while (std::chrono::steady_clock::now() < deadline && !std::filesystem::exists(stop)) {
            MSG message{};
            bool quit = false;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT)
                    quit = true;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (quit)
                break;
            checked(allocator->Reset());
            checked(commands->Reset(allocator.Get(), nullptr));
            const auto index = swap->GetCurrentBackBufferIndex();
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffers[index].Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            commands->ResourceBarrier(1, &barrier);
            const float cyan[]{0.f, 1.f, 1.f, 1.f};
            commands->ClearRenderTargetView(target_rtv, cyan, 0, nullptr);
            commands->ClearRenderTargetView({base.ptr + index * stride}, cyan, 0, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commands->ResourceBarrier(1, &barrier);
            checked(commands->Close());
            ID3D12CommandList *lists[]{commands.Get()};
            queue->ExecuteCommandLists(1, lists);
            checked(swap->Present(1, 0));
            checked(queue->Signal(fence.Get(), ++frame));
            checked(fence->SetEventOnCompletion(frame, event.get()));
            if (WaitForSingleObject(event.get(), 5000) != WAIT_OBJECT_0)
                throw std::runtime_error("Owned presenting fixture GPU wait timed out");
            Sleep(10);
        }
        std::cout << "Owned presenting fixture exited normally after " << frame << " frames.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
