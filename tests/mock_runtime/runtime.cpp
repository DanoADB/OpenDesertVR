// SPDX-License-Identifier: MIT
// Minimal test-only OpenXR runtime. Implements the subset used by odv-testhost,
// checks ordering/ownership, and provides WARP images. NOT a conformance runtime
// and NOT a simulated headset test. Never installed in the runtime package.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <d3d12.h>
#include <deque>
#include <dxgi1_6.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace {
using Microsoft::WRL::ComPtr;
struct Swapchain {
    std::array<ComPtr<ID3D12Resource>, 2> images;
    bool acquired = false, waited = false, released = false;
    unsigned image = 0;
};
struct Runtime {
    std::string scenario;
    bool instance = false, session = false, space = false, running = false, begun = false, waited = false;
    bool stopped = false, fault = false, timeout_sent = false, exit_requested = false;
    unsigned frames = 0, submissions = 0, acquisitions = 0, releases = 0, violations = 0;
    std::deque<XrSessionState> events;
    std::vector<Swapchain *> swapchains;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    bool is(const char *name) const { return scenario == name; }
    XrResult invalid() {
        ++violations;
        return XR_ERROR_CALL_ORDER_INVALID;
    }
} state;
int instance_token, session_token, space_token;
XrInstance instance_handle() { return reinterpret_cast<XrInstance>(&instance_token); }
XrSession session_handle() { return reinterpret_cast<XrSession>(&session_token); }
XrSpace space_handle() { return reinterpret_cast<XrSpace>(&space_token); }
Swapchain *chain(XrSwapchain handle) { return reinterpret_cast<Swapchain *>(handle); }
std::string environment(const char *key) {
    char buffer[32768]{};
    const DWORD count = GetEnvironmentVariableA(key, buffer, sizeof(buffer));
    return count && count < sizeof(buffer) ? std::string(buffer, count) : std::string{};
}
template <class T> XrResult enumerate(uint32_t capacity, uint32_t *count, T *out, const std::vector<T> &values) {
    *count = static_cast<uint32_t>(values.size());
    if (!capacity)
        return XR_SUCCESS;
    if (capacity < values.size())
        return XR_ERROR_SIZE_INSUFFICIENT;
    std::copy(values.begin(), values.end(), out);
    return XR_SUCCESS;
}
XrTime display_time() { return 1000000000ll + static_cast<XrTime>(state.frames) * 11111111ll; }
XrResult XRAPI_CALL extensions(const char *, uint32_t capacity, uint32_t *count, XrExtensionProperties *out) {
    if (state.scenario.empty())
        state.scenario = environment("ODV_MOCK_SCENARIO");
    XrExtensionProperties extension{XR_TYPE_EXTENSION_PROPERTIES};
    strcpy_s(extension.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
    extension.extensionVersion = XR_KHR_D3D12_enable_SPEC_VERSION;
    return enumerate(capacity, count, out,
                     state.is("missing_extension") ? std::vector<XrExtensionProperties>{}
                                                   : std::vector<XrExtensionProperties>{extension});
}
XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo *info, XrInstance *out) {
    if (state.instance)
        return XR_ERROR_LIMIT_REACHED;
    if (info->enabledExtensionCount != 1 || strcmp(info->enabledExtensionNames[0], XR_KHR_D3D12_ENABLE_EXTENSION_NAME))
        return XR_ERROR_EXTENSION_NOT_PRESENT;
    state.instance = true;
    *out = instance_handle();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_instance(XrInstance) {
    if (state.session || state.space || !state.swapchains.empty())
        ++state.violations;
    const auto path = environment("ODV_MOCK_AUDIT");
    if (!path.empty()) {
        const nlohmann::json audit = {{"test_runtime", true},
                                      {"frames", state.frames},
                                      {"stereo_submissions", state.submissions},
                                      {"acquisitions", state.acquisitions},
                                      {"releases", state.releases},
                                      {"violations", state.violations},
                                      {"session_destroyed", !state.session},
                                      {"space_destroyed", !state.space},
                                      {"swapchains_remaining", state.swapchains.size()},
                                      {"request_exit_called", state.exit_requested}};
        std::ofstream file(path);
        file << audit.dump(2);
    }
    state.instance = false;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL instance_properties(XrInstance, XrInstanceProperties *properties) {
    properties->runtimeVersion = XR_MAKE_VERSION(0, 1, 0);
    strcpy_s(properties->runtimeName, "OpenDesertVR Test Runtime");
    return XR_SUCCESS;
}
XrResult XRAPI_CALL get_system(XrInstance, const XrSystemGetInfo *, XrSystemId *out) {
    if (state.is("missing_headset"))
        return XR_ERROR_FORM_FACTOR_UNAVAILABLE;
    *out = 1;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL requirements(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR *out) {
    if (state.is("wrong_adapter")) {
        out->adapterLuid = {0xffffffffu, -1};
        out->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        return XR_SUCCESS;
    }
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> warp;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(warp->GetDesc1(&desc)))
        return XR_ERROR_RUNTIME_FAILURE;
    out->adapterLuid = desc.AdapterLuid;
    out->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL views(XrInstance, XrSystemId, XrViewConfigurationType type, uint32_t capacity, uint32_t *count,
                          XrViewConfigurationView *out) {
    if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    XrViewConfigurationView view{XR_TYPE_VIEW_CONFIGURATION_VIEW};
    view.recommendedImageRectWidth = view.recommendedImageRectHeight = 256;
    view.maxImageRectWidth = view.maxImageRectHeight = 256;
    view.recommendedSwapchainSampleCount = view.maxSwapchainSampleCount = 1;
    return enumerate(capacity, count, out, std::vector<XrViewConfigurationView>{view, view});
}
XrResult XRAPI_CALL blends(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t *count,
                           XrEnvironmentBlendMode *out) {
    return enumerate(capacity, count, out, std::vector<XrEnvironmentBlendMode>{XR_ENVIRONMENT_BLEND_MODE_OPAQUE});
}
XrResult XRAPI_CALL create_session(XrInstance, const XrSessionCreateInfo *info, XrSession *out) {
    auto binding = static_cast<const XrGraphicsBindingD3D12KHR *>(info->next);
    if (!binding || binding->type != XR_TYPE_GRAPHICS_BINDING_D3D12_KHR || !binding->device || !binding->queue)
        return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    state.device = binding->device;
    state.queue = binding->queue;
    state.session = true;
    state.events.push_back(XR_SESSION_STATE_READY);
    *out = session_handle();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_session(XrSession) {
    if (!state.swapchains.empty() || state.space || (state.begun && !state.fault))
        ++state.violations;
    state.session = false;
    state.queue.Reset();
    state.device.Reset();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL create_space(XrSession, const XrReferenceSpaceCreateInfo *info, XrSpace *out) {
    if (info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL || info->poseInReferenceSpace.orientation.w != 1)
        return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
    state.space = true;
    *out = space_handle();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_space(XrSpace) {
    state.space = false;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL formats(XrSession, uint32_t capacity, uint32_t *count, int64_t *out) {
    return enumerate(capacity, count, out, std::vector<int64_t>{DXGI_FORMAT_R8G8B8A8_UNORM});
}
XrResult XRAPI_CALL create_swapchain(XrSession, const XrSwapchainCreateInfo *info, XrSwapchain *out) {
    if (state.is("allocation_failure") || (state.is("second_allocation_failure") && state.swapchains.size() == 1)) {
        state.fault = true;
        return XR_ERROR_OUT_OF_MEMORY;
    }
    if (info->width != 256 || info->height != 256 || info->arraySize != 1 || info->sampleCount != 1 ||
        info->usageFlags != XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)
        return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    auto sc = std::make_unique<Swapchain>();
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = info->width;
    desc.Height = info->height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = static_cast<DXGI_FORMAT>(info->format);
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for (auto &image : sc->images)
        if (FAILED(state.device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&image))))
            return XR_ERROR_OUT_OF_MEMORY;
    *out = reinterpret_cast<XrSwapchain>(sc.get());
    state.swapchains.push_back(sc.release());
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_swapchain(XrSwapchain handle) {
    auto sc = chain(handle);
    if (sc->acquired && !state.fault)
        ++state.violations;
    state.swapchains.erase(std::remove(state.swapchains.begin(), state.swapchains.end(), sc), state.swapchains.end());
    delete sc;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL images(XrSwapchain handle, uint32_t capacity, uint32_t *count, XrSwapchainImageBaseHeader *out) {
    *count = 2;
    if (!capacity)
        return XR_SUCCESS;
    if (capacity < 2)
        return XR_ERROR_SIZE_INSUFFICIENT;
    auto typed = reinterpret_cast<XrSwapchainImageD3D12KHR *>(out);
    for (int i = 0; i < 2; ++i) {
        typed[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
        typed[i].texture = chain(handle)->images[i].Get();
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL begin_session(XrSession, const XrSessionBeginInfo *info) {
    if (state.is("begin_session_failure")) {
        state.fault = true;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    if (state.running || info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        return state.invalid();
    state.running = true;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL end_session(XrSession) {
    if (!state.stopped || state.begun)
        return state.invalid();
    state.running = false;
    state.events.push_back(XR_SESSION_STATE_EXITING);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL request_exit(XrSession) {
    state.exit_requested = true;
    state.events.push_back(XR_SESSION_STATE_STOPPING);
    state.stopped = true;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL poll(XrInstance, XrEventDataBuffer *out) {
    if (state.events.empty() && state.running && !state.stopped && !state.is("request_exit")) {
        if (state.frames >= 2 && state.is("instance_loss")) {
            XrEventDataInstanceLossPending event{XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING};
            event.lossTime = display_time();
            memcpy(out, &event, sizeof(event));
            state.stopped = true;
            state.fault = true;
            return XR_SUCCESS;
        }
        if (state.frames >= 2 && state.is("session_loss")) {
            state.events.push_back(XR_SESSION_STATE_LOSS_PENDING);
            state.stopped = true;
            state.fault = true;
        } else if (state.frames >= 6) {
            state.events.push_back(XR_SESSION_STATE_STOPPING);
            state.stopped = true;
        }
    }
    if (state.events.empty())
        return XR_EVENT_UNAVAILABLE;
    XrEventDataSessionStateChanged event{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    event.session = session_handle();
    event.state = state.events.front();
    event.time = display_time();
    state.events.pop_front();
    memcpy(out, &event, sizeof(event));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL wait_frame(XrSession, const XrFrameWaitInfo *, XrFrameState *out) {
    if (state.is("wait_frame_failure")) {
        state.fault = true;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    if (!state.running || state.waited || state.begun)
        return state.invalid();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    state.waited = true;
    out->predictedDisplayTime = display_time();
    out->predictedDisplayPeriod = 11111111;
    out->shouldRender = !(state.is("all_skip") || (state.is("skip_render") && state.frames % 2 == 0));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL begin_frame(XrSession, const XrFrameBeginInfo *) {
    if (state.is("begin_frame_failure")) {
        state.fault = true;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    if (!state.waited || state.begun)
        return state.invalid();
    state.waited = false;
    state.begun = true;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL locate(XrSession, const XrViewLocateInfo *info, XrViewState *view_state, uint32_t capacity,
                           uint32_t *count, XrView *out) {
    if (state.is("locate_failure")) {
        state.fault = true;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    if (!state.begun || info->displayTime != display_time() || info->space != space_handle() || capacity < 2)
        return state.invalid();
    *count = 2;
    view_state->viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if (state.is("invalid_tracking") && state.frames % 2 == 0)
        view_state->viewStateFlags = 0;
    for (unsigned i = 0; i < 2; ++i) {
        out[i].pose.orientation = {0, 0, 0, 1};
        out[i].pose.position = {i ? 0.032f : -0.032f, 0, 0};
        out[i].fov = {-0.65f, 0.75f, 0.7f, -0.7f};
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL acquire(XrSwapchain handle, const XrSwapchainImageAcquireInfo *, uint32_t *out) {
    auto sc = chain(handle);
    if (!state.begun || sc->acquired)
        return state.invalid();
    if (state.is("acquire_failure") || (state.is("second_acquire_failure") && sc == state.swapchains[1])) {
        state.fault = true;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    sc->acquired = true;
    sc->waited = false;
    sc->released = false;
    sc->image = state.frames % 2;
    *out = sc->image;
    ++state.acquisitions;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL wait_image(XrSwapchain handle, const XrSwapchainImageWaitInfo *info) {
    auto sc = chain(handle);
    if (!sc->acquired || sc->waited)
        return state.invalid();
    if (state.is("wait_failure") || (state.is("second_wait_failure") && sc == state.swapchains[1])) {
        state.fault = true;
        return XR_ERROR_SESSION_LOST;
    }
    if (state.is("wait_timeout") && !state.timeout_sent) {
        state.timeout_sent = true;
        return XR_TIMEOUT_EXPIRED;
    }
    if (state.is("image_timeout")) {
        state.fault = true;
        if (info->timeout <= 0 || info->timeout > 100000000)
            return state.invalid();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return XR_TIMEOUT_EXPIRED;
    }
    sc->waited = true;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL release(XrSwapchain handle, const XrSwapchainImageReleaseInfo *) {
    auto sc = chain(handle);
    if (!sc->acquired || !sc->waited)
        return state.invalid();
    if (state.is("release_failure") || (state.is("second_release_failure") && sc == state.swapchains[1])) {
        state.fault = true;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    sc->acquired = false;
    sc->released = true;
    ++state.releases;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL end_frame(XrSession, const XrFrameEndInfo *info) {
    if (state.is("end_frame_failure")) {
        state.fault = true;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    if (!state.begun || info->displayTime != display_time())
        return state.invalid();
    if (info->layerCount) {
        if (info->layerCount != 1 || info->layers[0]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            return state.invalid();
        const auto layer = reinterpret_cast<const XrCompositionLayerProjection *>(info->layers[0]);
        if (layer->viewCount != 2 || layer->space != space_handle() ||
            layer->views[0].subImage.swapchain == layer->views[1].subImage.swapchain)
            return state.invalid();
        if (std::abs(layer->views[1].pose.position.x - layer->views[0].pose.position.x - 0.064f) > 1e-6f)
            return state.invalid();
        for (unsigned i = 0; i < 2; ++i) {
            const auto &view = layer->views[i];
            const auto sc = chain(view.subImage.swapchain);
            if (!sc->released || sc->acquired || view.subImage.imageRect.extent.width != 256 ||
                view.subImage.imageRect.extent.height != 256 || view.subImage.imageArrayIndex != 0)
                return state.invalid();
            sc->released = false;
        }
        ++state.submissions;
    }
    state.begun = false;
    ++state.frames;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL proc(XrInstance, const char *name, PFN_xrVoidFunction *out) {
    *out = nullptr;
    struct Entry {
        const char *name;
        PFN_xrVoidFunction function;
    };
    const Entry entries[] = {
        {"xrGetInstanceProcAddr", reinterpret_cast<PFN_xrVoidFunction>(proc)},
        {"xrEnumerateInstanceExtensionProperties", reinterpret_cast<PFN_xrVoidFunction>(extensions)},
        {"xrCreateInstance", reinterpret_cast<PFN_xrVoidFunction>(create_instance)},
        {"xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction>(destroy_instance)},
        {"xrGetInstanceProperties", reinterpret_cast<PFN_xrVoidFunction>(instance_properties)},
        {"xrGetSystem", reinterpret_cast<PFN_xrVoidFunction>(get_system)},
        {"xrGetD3D12GraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction>(requirements)},
        {"xrEnumerateViewConfigurationViews", reinterpret_cast<PFN_xrVoidFunction>(views)},
        {"xrEnumerateEnvironmentBlendModes", reinterpret_cast<PFN_xrVoidFunction>(blends)},
        {"xrCreateSession", reinterpret_cast<PFN_xrVoidFunction>(create_session)},
        {"xrDestroySession", reinterpret_cast<PFN_xrVoidFunction>(destroy_session)},
        {"xrCreateReferenceSpace", reinterpret_cast<PFN_xrVoidFunction>(create_space)},
        {"xrDestroySpace", reinterpret_cast<PFN_xrVoidFunction>(destroy_space)},
        {"xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction>(formats)},
        {"xrCreateSwapchain", reinterpret_cast<PFN_xrVoidFunction>(create_swapchain)},
        {"xrDestroySwapchain", reinterpret_cast<PFN_xrVoidFunction>(destroy_swapchain)},
        {"xrEnumerateSwapchainImages", reinterpret_cast<PFN_xrVoidFunction>(images)},
        {"xrBeginSession", reinterpret_cast<PFN_xrVoidFunction>(begin_session)},
        {"xrEndSession", reinterpret_cast<PFN_xrVoidFunction>(end_session)},
        {"xrRequestExitSession", reinterpret_cast<PFN_xrVoidFunction>(request_exit)},
        {"xrPollEvent", reinterpret_cast<PFN_xrVoidFunction>(poll)},
        {"xrWaitFrame", reinterpret_cast<PFN_xrVoidFunction>(wait_frame)},
        {"xrBeginFrame", reinterpret_cast<PFN_xrVoidFunction>(begin_frame)},
        {"xrLocateViews", reinterpret_cast<PFN_xrVoidFunction>(locate)},
        {"xrAcquireSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(acquire)},
        {"xrWaitSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(wait_image)},
        {"xrReleaseSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(release)},
        {"xrEndFrame", reinterpret_cast<PFN_xrVoidFunction>(end_frame)},
    };
    for (const auto &entry : entries)
        if (!strcmp(name, entry.name)) {
            *out = entry.function;
            return XR_SUCCESS;
        }

    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
} // namespace
extern "C" __declspec(dllexport) XrResult XRAPI_CALL
xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo *loader, XrNegotiateRuntimeRequest *runtime) {
    if (!loader || !runtime || loader->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
        loader->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    state.scenario = environment("ODV_MOCK_SCENARIO");
    runtime->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtime->runtimeApiVersion = XR_API_VERSION_1_0;
    runtime->getInstanceProcAddr = proc;
    return XR_SUCCESS;
}
