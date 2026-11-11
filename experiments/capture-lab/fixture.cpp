// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <iostream>
#include <memory>
#include <renderdoc_app.h>
#include <stdexcept>
#include <string>
#include <windows.h>
#include <wrl/client.h>

template <class T> using Com = Microsoft::WRL::ComPtr<T>;
void checked(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("Owned D3D12 fixture operation failed");
}
std::string utf8(const wchar_t *text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size < 1)
        throw std::runtime_error("Invalid capture prefix");
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Provide the owned-fixture capture prefix");
        const auto module = GetModuleHandleW(L"renderdoc.dll");
        const auto get_api =
            module ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI")) : nullptr;
        RENDERDOC_API_1_6_0 *capture = nullptr;
        if (!get_api || get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void **>(&capture)) != 1)
            throw std::runtime_error("Launch this owned fixture through RenderDoc capture");
        const auto prefix = utf8(argv[1]);
        capture->SetCaptureFilePathTemplate(prefix.c_str());
        Com<ID3D12Device> device;
        checked(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        capture->StartFrameCapture(device.Get(), nullptr);
        if (!capture->IsFrameCapturing())
            throw std::runtime_error("RenderDoc did not begin the explicit headless capture");
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture_desc{};
        texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_desc.Width = 16;
        texture_desc.Height = 16;
        texture_desc.DepthOrArraySize = 1;
        texture_desc.MipLevels = 1;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Com<ID3D12Resource> texture;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                                D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture)));
        texture->SetName(L"OpenDesertVR owned capture-lab color target");
        D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
        rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_desc.NumDescriptors = 1;
        Com<ID3D12DescriptorHeap> rtv;
        checked(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv)));
        const auto handle = rtv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(texture.Get(), nullptr, handle);
        Com<ID3D12CommandAllocator> allocator;
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList> commands;
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&commands)));
        // Endpoint channels avoid fractional UNORM conversion/rounding differences.
        const float color[]{0.f, 1.f, 1.f, 1.f};
        commands->ClearRenderTargetView(handle, color, 0, nullptr);
        checked(commands->Close());
        ID3D12CommandList *lists[]{commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        Com<ID3D12Fence> fence;
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        const std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr),
                                                                  CloseHandle);
        if (!event)
            throw std::runtime_error("Could not create fixture fence event");
        checked(queue->Signal(fence.Get(), 1));
        checked(fence->SetEventOnCompletion(1, event.get()));
        if (WaitForSingleObject(event.get(), 5000) != WAIT_OBJECT_0)
            throw std::runtime_error("Owned fixture GPU completion timed out");
        if (!capture->EndFrameCapture(device.Get(), nullptr) || !capture->GetNumCaptures())
            throw std::runtime_error("Owned capture was not written");
        std::cout << "Owned D3D12 clear captured; no game was launched or attached.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
