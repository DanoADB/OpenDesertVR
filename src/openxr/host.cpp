// SPDX-License-Identifier: MIT
#include "odv/openxr/host.hpp"
#include "odv/core/metrics.hpp"
#include "odv/d3d12/renderer.hpp"
#include "odv/openxr/lifecycle.hpp"
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <optional>
#include <thread>

namespace odv::xr {
namespace {
void xrcheck(XrResult result, const char *operation) {
    if (XR_FAILED(result) || result == XR_SESSION_LOSS_PENDING) {
        ErrorCode code = ErrorCode::session;
        if (result == XR_ERROR_RUNTIME_UNAVAILABLE || result == XR_ERROR_INITIALIZATION_FAILED)
            code = ErrorCode::runtime;
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE || result == XR_ERROR_FORM_FACTOR_UNSUPPORTED)
            code = ErrorCode::headset;
        if (result == XR_ERROR_EXTENSION_NOT_PRESENT)
            code = ErrorCode::extension;
        if (result == XR_ERROR_OUT_OF_MEMORY)
            code = ErrorCode::allocation;
        throw Failure(code, std::string(operation) + " (OpenXR " + std::to_string(result) + ")");
    }
}
std::atomic_bool interrupted = false;
BOOL WINAPI control(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        interrupted = true;
        return TRUE;
    }
    return FALSE;
}
class Host final : public IXrFrame {
  public:
    XrInstance instance{};
    XrSystemId system{};
    XrSession session{};
    XrSpace space{};
    struct Eye {
        XrSwapchain swapchain{};
        int32_t width{}, height{};
        std::vector<XrSwapchainImageD3D12KHR> images;
    };
    std::array<Eye, 2> eyes;
    d3d::Renderer graphics;
    Lifecycle lifecycle;
    Log log;
    Config config;
    DXGI_FORMAT format{};
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    XrFrameState frame_state{XR_TYPE_FRAME_STATE};
    std::optional<XrTime> first_display_time;
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    std::array<XrCompositionLayerProjectionView, 2> layers{
        {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
    bool frame_open = false, exit_requested = false;
    uint64_t frames = 0, rendered = 0;
    TimingWindow timings;
    std::array<TimingWindow, 2> gpu_timings;
    TimingWindow gpu_stereo_timings;
    std::array<std::optional<double>, 2> frame_gpu_ms;

    ~Host() {
        // Complete application GPU work before runtime-owned textures/session die.
        if (graphics.queue()) {
            try {
                graphics.sync();
            } catch (...) {
            }
        }
        for (auto &eye : eyes)
            if (eye.swapchain)
                xrDestroySwapchain(eye.swapchain);
        if (space)
            xrDestroySpace(space);
        if (session)
            xrDestroySession(session);
        if (instance)
            xrDestroyInstance(instance);
    }
    void initialize(Json &report, bool create_session) {
        auto &checks = report["checks"];
        uint32_t count = 0;
        xrcheck(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr), "enumerate runtime extensions");
        checks.push_back(check("runtime_discovery", "PASS", "OpenXR loader reached an active runtime"));
        std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
        xrcheck(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()),
                "extension properties");
        const bool d3d = std::any_of(extensions.begin(), extensions.end(), [](const auto &e) {
            return std::string(e.extensionName) == XR_KHR_D3D12_ENABLE_EXTENSION_NAME;
        });
        report["d3d12_extension"] = d3d;
        validate({true, true, d3d, true, true, true});
        checks.push_back(check("d3d12_extension", "PASS", "XR_KHR_D3D12_enable available"));
        const char *extension = XR_KHR_D3D12_ENABLE_EXTENSION_NAME;
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(info.applicationInfo.applicationName, "odv-testhost");
        info.applicationInfo.applicationVersion = 1;
        info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        info.enabledExtensionCount = 1;
        info.enabledExtensionNames = &extension;
        xrcheck(xrCreateInstance(&info, &instance), "create XR instance");
        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
        xrcheck(xrGetInstanceProperties(instance, &properties), "runtime properties");
        report["openxr_runtime_version"] = {{"major", XR_VERSION_MAJOR(properties.runtimeVersion)},
                                            {"minor", XR_VERSION_MINOR(properties.runtimeVersion)},
                                            {"patch", XR_VERSION_PATCH(properties.runtimeVersion)}};
        report["test_runtime"] = std::string(properties.runtimeName) == "OpenDesertVR Test Runtime";
        XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
        system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        xrcheck(xrGetSystem(instance, &system_info, &system), "get headset system (connect and wake PCVR headset)");
        checks.push_back(check("headset_system", "PASS", "HMD system available; visual verification still NOT_RUN"));
        PFN_xrGetD3D12GraphicsRequirementsKHR requirements_fn{};
        xrcheck(xrGetInstanceProcAddr(instance, "xrGetD3D12GraphicsRequirementsKHR",
                                      reinterpret_cast<PFN_xrVoidFunction *>(&requirements_fn)),
                "graphics requirements function");
        XrGraphicsRequirementsD3D12KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
        xrcheck(requirements_fn(instance, system, &requirements), "D3D12 requirements");
        graphics.initialize(requirements.adapterLuid, requirements.minFeatureLevel, config.debug_layer);
        checks.push_back(check("runtime_gpu_device", "PASS",
                               "D3D12 device and queue created on runtime-matched adapter; identifiers redacted"));
        if (!create_session) {
            checks.push_back(check("session_rendering", "NOT_RUN", "doctor does not create a presentation session"));
            return;
        }
        uint32_t view_count = 0;
        xrcheck(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                  &view_count, nullptr),
                "stereo view count");
        if (view_count != 2)
            throw Failure(ErrorCode::session, "exactly two primary stereo views required");
        std::array<XrViewConfigurationView, 2> view_info{
            {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        xrcheck(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                                  &view_count, view_info.data()),
                "stereo views");
        xrcheck(xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count,
                                                 nullptr),
                "blend modes");
        std::vector<XrEnvironmentBlendMode> blends(count);
        xrcheck(xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count,
                                                 &count, blends.data()),
                "blend modes");
        if (std::find(blends.begin(), blends.end(), blend) == blends.end())
            throw Failure(ErrorCode::session, "opaque PCVR blend mode required");
        XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
        binding.device = graphics.device();
        binding.queue = graphics.queue();
        XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
        session_info.next = &binding;
        session_info.systemId = system;
        xrcheck(xrCreateSession(instance, &session_info, &session), "create session");
        XrReferenceSpaceCreateInfo ref{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        ref.poseInReferenceSpace.orientation.w = 1;
        xrcheck(xrCreateReferenceSpace(session, &ref, &space), "LOCAL reference space");
        xrcheck(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "swapchain formats");
        std::vector<int64_t> formats(count);
        xrcheck(xrEnumerateSwapchainFormats(session, count, &count, formats.data()), "swapchain formats");
        for (auto candidate : {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                               DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM})
            if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
                format = candidate;
                break;
            }
        if (!format)
            throw Failure(ErrorCode::session, "no supported color format");
        graphics.pipeline(format);
        for (unsigned i = 0; i < 2; ++i) {
            auto &eye = eyes[i];
            eye.width = static_cast<int32_t>(view_info[i].recommendedImageRectWidth);
            eye.height = static_cast<int32_t>(view_info[i].recommendedImageRectHeight);
            XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            sc.format = format;
            sc.sampleCount = 1;
            sc.width = eye.width;
            sc.height = eye.height;
            sc.faceCount = 1;
            sc.arraySize = 1;
            sc.mipCount = 1;
            xrcheck(xrCreateSwapchain(session, &sc, &eye.swapchain), "create eye swapchain");
            xrcheck(xrEnumerateSwapchainImages(eye.swapchain, 0, &count, nullptr), "eye image count");
            eye.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
            xrcheck(xrEnumerateSwapchainImages(eye.swapchain, count, &count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader *>(eye.images.data())),
                    "eye images");
        }
        checks.push_back(
            check("session_resources", "PASS", "stereo session, reference space and two eye swapchains created"));
    }
    void poll() {
        for (;;) {
            XrEventDataBuffer buffer{XR_TYPE_EVENT_DATA_BUFFER};
            const auto result = xrPollEvent(instance, &buffer);
            if (result == XR_EVENT_UNAVAILABLE)
                break;
            xrcheck(result, "poll events");
            if (buffer.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                lifecycle.lose();
                log.record("INSTANCE_LOSS");
                break;
            }
            if (buffer.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
                continue;
            const auto &event = *reinterpret_cast<XrEventDataSessionStateChanged *>(&buffer);
            if (event.session != session)
                continue;
            log.record("SESSION_STATE", {{"state", static_cast<int>(event.state)}});
            std::cout << "session_state=" << event.state << '\n';
            switch (event.state) {
            case XR_SESSION_STATE_READY: {
                lifecycle.ready();
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrcheck(xrBeginSession(session, &begin), "begin session");
                lifecycle.begin();
                break;
            }
            case XR_SESSION_STATE_STOPPING:
                lifecycle.stop();
                xrcheck(xrEndSession(session), "end session");
                lifecycle.end();
                if (exit_requested)
                    lifecycle.exit();
                break;
            case XR_SESSION_STATE_LOSS_PENDING:
                lifecycle.lose();
                break;
            case XR_SESSION_STATE_EXITING:
                lifecycle.exit();
                break;
            default:
                break;
            }
        }
    }
    bool wait_begin() override {
        frame_gpu_ms = {};
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        xrcheck(xrWaitFrame(session, &wait, &frame_state), "wait frame");
        if (!first_display_time)
            first_display_time = frame_state.predictedDisplayTime;
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        xrcheck(xrBeginFrame(session, &begin), "begin frame");
        frame_open = true;
        return frame_state.shouldRender == XR_TRUE;
    }
    bool locate() override {
        XrViewLocateInfo locate_info{XR_TYPE_VIEW_LOCATE_INFO};
        locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate_info.displayTime = frame_state.predictedDisplayTime;
        locate_info.space = space;
        XrViewState state{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        xrcheck(xrLocateViews(session, &locate_info, &state, 2, &count, views.data()), "locate views");
        return count == 2 &&
               (state.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT)) ==
                   (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    }
    void eye(unsigned index) override {
        auto &eye = eyes.at(index);
        uint32_t image = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        xrcheck(xrAcquireSwapchainImage(eye.swapchain, &acquire, &image), "acquire eye image");
        // An acquired image must be successfully waited before release. On a wait
        // failure the session is torn down; never illegally release an unwaited image.
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 100000000; // 100 ms, so a timing-out runtime cannot trap the host forever.
        const auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        XrResult waited;
        do {
            waited = xrWaitSwapchainImage(eye.swapchain, &wait);
            if (waited == XR_TIMEOUT_EXPIRED) {
                if (interrupted || std::chrono::steady_clock::now() >= wait_deadline)
                    throw Failure(ErrorCode::session, "eye image wait cancelled or exceeded five seconds");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } while (waited == XR_TIMEOUT_EXPIRED);
        xrcheck(waited, "wait eye image");
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        try {
            const auto &v = views[index];
            const auto &p = v.pose;
            math::Pose pose{{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                            {p.position.x, p.position.y, p.position.z}};
            auto geometry = stereo::vertices(pose, {v.fov.angleLeft, v.fov.angleRight, v.fov.angleUp, v.fov.angleDown},
                                             config.near_z, config.far_z);
            graphics.render(eye.images.at(image).texture, format, eye.width, eye.height, geometry);
            frame_gpu_ms[index] = graphics.last_gpu_ms();
            if (frame_gpu_ms[index])
                gpu_timings[index].add(*frame_gpu_ms[index]);
            auto &layer = layers[index];
            layer.pose = v.pose;
            layer.fov = v.fov;
            layer.subImage = {eye.swapchain, {{0, 0}, {eye.width, eye.height}}, 0};
            if (frames % 120 == 0) {
                log.record("EYE_GEOMETRY",
                           {{"eye", index},
                            {"frame_index", frames},
                            {"display_time_from_start_ns", frame_state.predictedDisplayTime - *first_display_time},
                            {"display_period_ns", frame_state.predictedDisplayPeriod},
                            {"x", p.position.x},
                            {"y", p.position.y},
                            {"z", p.position.z},
                            {"qx", p.orientation.x},
                            {"qy", p.orientation.y},
                            {"qz", p.orientation.z},
                            {"qw", p.orientation.w},
                            {"left", v.fov.angleLeft},
                            {"right", v.fov.angleRight},
                            {"up", v.fov.angleUp},
                            {"down", v.fov.angleDown},
                            {"vertices", geometry.size()}});
                std::cout << "eye=" << index << " position_m=" << p.position.x << ',' << p.position.y << ','
                          << p.position.z << " vertices=" << geometry.size() << '\n';
            }
        } catch (...) {
            try {
                graphics.sync();
                xrReleaseSwapchainImage(eye.swapchain, &release);
            } catch (...) {
                // Device loss: leave image ownership to session teardown.
            }
            throw;
        }
        xrcheck(xrReleaseSwapchainImage(eye.swapchain, &release), "release eye image");
    }
    void end(bool submit) override {
        if (!frame_open)
            return;
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space;
        layer.viewCount = 2;
        layer.views = layers.data();
        const XrCompositionLayerBaseHeader *base = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
        XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
        end_info.displayTime = frame_state.predictedDisplayTime;
        end_info.environmentBlendMode = blend;
        end_info.layerCount = submit ? 1 : 0;
        end_info.layers = submit ? &base : nullptr;
        frame_open = false;
        xrcheck(xrEndFrame(session, &end_info), "end frame");
        ++frames;
        if (submit) {
            ++rendered;
            if (frame_gpu_ms[0] && frame_gpu_ms[1])
                gpu_stereo_timings.add(*frame_gpu_ms[0] + *frame_gpu_ms[1]);
        }
    }
};
void failure(Json &report, const Failure &error) {
    static const char *names[] = {"configuration", "runtime",    "headset", "extension",
                                  "adapter",       "allocation", "device",  "session"};
    static const char *details[] = {"Invalid version-1 configuration; check ranges and types",
                                    "No usable OpenXR runtime; install/select a Windows x64 OpenXR runtime",
                                    "No headset system; connect and wake a PCVR headset in the active runtime",
                                    "XR_KHR_D3D12_enable missing; select a runtime supporting D3D12",
                                    "Runtime-requested GPU adapter unavailable; check runtime and GPU routing",
                                    "Graphics allocation failed; check available memory and graphics tools",
                                    "D3D12 device or graphics operation failed; check drivers and local console",
                                    "OpenXR session operation failed or session lost; restart the runtime/testhost"};
    report["checks"].push_back(
        check(names[static_cast<int>(error.code)], "FAIL", details[static_cast<int>(error.code)]));
    report["ready"] = false;
}
} // namespace
Json doctor() {
    auto report = empty_report();
    Host host;
    // Basic GPU discovery does not require an HMD or an OpenXR system.
    try {
        d3d::Com<IDXGIFactory4> factory;
        d3d::hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI discovery");
        unsigned adapters = 0, supported = 0;
        for (UINT i = 0;; ++i) {
            d3d::Com<IDXGIAdapter1> adapter;
            const auto result = factory->EnumAdapters1(i, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND)
                break;
            d3d::hr(result, "GPU enumeration");
            DXGI_ADAPTER_DESC1 desc{};
            d3d::hr(adapter->GetDesc1(&desc), "GPU description");
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            ++adapters;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
                ++supported;
        }
        report["gpu"] = {
            {"hardware_adapters", adapters}, {"d3d12_capable_adapters", supported}, {"identifiers", "REDACTED"}};
        report["checks"].push_back(check("basic_gpu", supported ? "PASS" : "FAIL",
                                         supported ? "D3D12 hardware adapter found; XR match checked separately"
                                                   : "No D3D12 hardware adapter detected"));
    } catch (const Failure &) {
        report["checks"].push_back(check("basic_gpu", "FAIL", "GPU discovery failed"));
    }
    try {
        host.initialize(report, false);
        report["ready"] = true;
    } catch (const Failure &e) {
        failure(report, e);
    }
    return report;
}
Json run(const Config &config) {
    auto report = empty_report();
    Host host;
    host.config = config;
    interrupted = false;
    SetConsoleCtrlHandler(control, TRUE);
    try {
        host.initialize(report, true);
        const auto start = std::chrono::steady_clock::now();
        auto exit_deadline = start + std::chrono::seconds(config.duration_seconds + 10);
        while (!host.lifecycle.terminal()) {
            host.poll();
            const auto now = std::chrono::steady_clock::now();
            if (interrupted || now - start >= std::chrono::seconds(config.duration_seconds)) {
                if (!host.exit_requested) {
                    if (host.lifecycle.running())
                        xrcheck(xrRequestExitSession(host.session), "request exit");
                    else
                        break;
                    host.exit_requested = true;
                    exit_deadline = now + std::chrono::seconds(10);
                }
                if (now >= exit_deadline)
                    throw Failure(ErrorCode::session, "runtime did not stop after exit request");
            }
            if (host.lifecycle.running()) {
                const auto before = std::chrono::steady_clock::now();
                frame(host);
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
                host.timings.add(ms);
            } else
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (host.lifecycle.lost())
            throw Failure(ErrorCode::session, "OpenXR session or instance lost; restart testhost");
        report["ready"] = host.rendered > 0;
        report["checks"].push_back(check("stereo_submission", host.rendered ? "PASS" : "NOT_RUN",
                                         host.rendered ? "Synthetic frames submitted; visual correctness UNVERIFIED"
                                                       : "No valid stereo frame submitted"));
    } catch (const Failure &e) {
        std::cerr << e.what() << '\n';
        failure(report, e);
    } catch (const std::exception &) {
        report["ready"] = false;
        report["checks"].push_back(check("host", "FAIL", "Unexpected host failure"));
    }
    SetConsoleCtrlHandler(control, FALSE);
    report["frames"] = host.frames;
    report["stereo_frames"] = host.rendered;
    report["events"] = host.log.snapshot();
    report["cpu_frame_ms"] = host.timings.summary();
    report["gpu_timing"] = host.gpu_stereo_timings.empty() ? "NOT_RUN" : "PASS";
    report["gpu_eye_ms"] = Json::array({host.gpu_timings[0].summary(), host.gpu_timings[1].summary()});
    report["gpu_stereo_ms"] = host.gpu_stereo_timings.summary();
    report["timing_scope"] = "CPU includes runtime wait and GPU fence; GPU covers eye clear/draw commands only, "
                             "excludes compositor/display latency";
    report["reprojection_metrics"] = "NOT_RUN";
    return report;
}
} // namespace odv::xr
