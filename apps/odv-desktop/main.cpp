// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// Windows base declarations must precede the common-control and shell headers.
// clang-format off
#include <windows.h>
// clang-format on
#include "odv/openxr/host.hpp"
#include <chrono>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>

namespace {
constexpr UINT finished = WM_APP + 1;
constexpr int start_id = 101, doctor_id = 102, stop_id = 103, folder_id = 104, save_id = 105, mode_id = 106;
struct App {
    HWND window{}, status{}, detail{}, start{}, doctor{}, stop{}, folder{}, save{}, notes{}, mode{};
    HWND visual[4]{};
    HFONT font{}, heading{};
    std::filesystem::path package, run_directory;
    std::jthread worker;
    std::mutex mutex;
    std::wstring message, explanation;
    std::wstring stream_channel;
    bool depth_preview = true;
    bool busy = false, closing = false, running_test = false, host_ran = false, test_runtime = false;
    std::chrono::steady_clock::time_point began;
};
App app;
std::wstring wide(const std::string &text) {
    if (text.empty())
        return {};
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L' ');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
std::string utf8(const std::wstring &text) {
    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, ' ');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
void write_json(const std::filesystem::path &path, const odv::Json &value) {
    std::ofstream file(path, std::ios::binary);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << value.dump(2) << '\n';
}
std::filesystem::path fresh_directory() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        throw std::runtime_error("run_id_failed");
    wchar_t guid[40]{};
    StringFromGUID2(id, guid, 40);
    auto root = app.package / L"reports";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    auto path = root / (std::wstring(L"run-") + guid);
    if (!error && std::filesystem::create_directory(path, error))
        return path;
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        throw std::runtime_error("report_directory_failed");
    root = std::filesystem::path(local) / L"OpenDesertVR" / L"reports";
    CoTaskMemFree(local);
    std::filesystem::create_directories(root);
    path = root / (std::wstring(L"run-") + guid);
    if (!std::filesystem::create_directory(path))
        throw std::runtime_error("fresh_run_directory_failed");
    return path;
}
std::wstring check_details(const odv::Json &report) {
    std::wstring out;
    for (const auto &check : report.at("checks"))
        out += wide(check.value("status", "UNKNOWN") + ": " + check.value("detail", "")) + L"\r\n";
    return out;
}
void operation(bool test) {
    if (app.busy)
        return;
    try {
        app.run_directory = fresh_directory();
        EnableWindow(app.folder, TRUE);
        app.host_ran = false;
        app.test_runtime = false;
        app.busy = true;
        app.running_test = test;
        app.began = std::chrono::steady_clock::now();
        EnableWindow(app.start, FALSE);
        EnableWindow(app.doctor, FALSE);
        EnableWindow(app.mode, FALSE);
        EnableWindow(app.stop, test);
        EnableWindow(app.save, FALSE);
        for (auto box : app.visual)
            SendMessageW(box, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(app.notes, L"");
        SetWindowTextW(app.status, L"Checking headset readiness...");
        SetWindowTextW(app.detail, L"Keep the Quest Pro awake in Quest Link. This window remains responsive.");
        const auto directory = app.run_directory;
        app.worker = std::jthread([directory, test](std::stop_token stop) {
            std::wstring message, details;
            bool host_ran = false, test_runtime = false;
            try {
                auto readiness = odv::xr::doctor();
                write_json(directory / L"doctor.json", readiness);
                test_runtime = readiness.value("test_runtime", false);
                odv::Json summary = {{"schema_version", 1},
                                     {"technical_submission", "NOT_RUN"},
                                     {"headset_validation", "NOT_RUN"},
                                     {"game_validation", "NOT_RUN"},
                                     {"host_invoked", false},
                                     {"test_runtime", test_runtime},
                                     {"runner", "OpenDesertVR desktop"}};
                summary["presentation_mode"] = app.depth_preview || !app.stream_channel.empty()
                                                   ? "STATIC_STEREO_IMAGE_STREAM"
                                                   : "SYNTHETIC_GEOMETRY";
                summary["owned_depth_preview"] = app.depth_preview;
                const auto build_path = app.package / L"build-info.json";
                if (std::filesystem::is_regular_file(build_path) && std::filesystem::file_size(build_path) < 65536) {
                    std::ifstream file(build_path);
                    auto build = odv::Json::parse(file);
                    summary["build"] = {{"source_commit", build.value("source_commit", "unknown")},
                                        {"source_dirty", build.value("source_dirty", true)}};
                }
                details = check_details(readiness);
                if (!readiness.value("ready", false)) {
                    message = L"Headset is not ready. See the checks below.";
                } else if (!test) {
                    message = L"Ready for the stereo test.";
                } else if (stop.stop_requested()) {
                    message = L"Test cancelled before starting.";
                    summary["cancelled"] = true;
                } else {
                    PostMessageW(app.window, WM_APP + 2, test_runtime ? 1 : 0, 0);
                    odv::Config config;
                    write_json(directory / L"test-config.json", {{"schema_version", 1},
                                                                 {"near_z", config.near_z},
                                                                 {"far_z", config.far_z},
                                                                 {"duration_seconds", 60},
                                                                 {"debug_layer", false}});
                    auto report = app.depth_preview            ? odv::xr::run_depth_preview(config, stop)
                                  : app.stream_channel.empty() ? odv::xr::run(config, stop)
                                                               : odv::xr::run_stream(config, app.stream_channel, stop);
                    host_ran = true;
                    test_runtime = report.value("test_runtime", false);
                    write_json(directory / L"host.json", report);
                    const bool passed = report.value("ready", false);
                    summary["host_invoked"] = true;
                    summary["test_runtime"] = test_runtime;
                    summary["technical_submission"] = passed ? "PASS" : "FAIL";
                    summary["stereo_frames"] = report.value("stereo_frames", 0ULL);
                    summary["source_pairs_received"] = report.value("source_pairs_received", 0ULL);
                    summary["stereo_technique"] = report.value("stereo_technique", "INDEPENDENT_SYNTHETIC");
                    summary["stop_requested"] = stop.stop_requested();
                    message = passed ? L"Stereo frames submitted. Record what you saw below."
                                     : L"Test did not pass. See the checks below.";
                    details = L"Stereo frames: " + std::to_wstring(report.value("stereo_frames", 0ULL)) + L"\r\n" +
                              check_details(report);
                    if (app.depth_preview || !app.stream_channel.empty())
                        details = L"Technique: " + wide(report.value("stereo_technique", "UNAVAILABLE")) +
                                  L" | Source pairs: " + std::to_wstring(report.value("source_pairs_received", 0ULL)) +
                                  L"\r\n" + details;
                }
                write_json(directory / L"run-summary.json", summary);
                if (test_runtime)
                    message = host_ran ? L"Simulated stereo check finished. Visual observations are unavailable."
                                       : L"SIMULATED RUNTIME: software test only. " + message;
            } catch (const std::exception &) {
                message = L"Could not complete the test or save its reports.";
                details = L"Use a writable folder and confirm the OpenXR runtime is available. Earlier reports are "
                          L"preserved.";
            }
            {
                std::lock_guard lock(app.mutex);
                app.message = std::move(message);
                app.explanation = std::move(details);
                app.host_ran = host_ran;
                app.test_runtime = test_runtime;
            }
            PostMessageW(app.window, finished, 0, 0);
        });
    } catch (const std::exception &) {
        app.busy = false;
        EnableWindow(app.start, TRUE);
        EnableWindow(app.doctor, TRUE);
        EnableWindow(app.mode, app.stream_channel.empty());
        EnableWindow(app.stop, FALSE);
        EnableWindow(app.folder, FALSE);
        SetWindowTextW(app.status, L"Could not create a fresh report folder. Try a writable location.");
    }
}
HWND control(const wchar_t *kind, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id = 0) {
    const auto handle =
        CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, app.window,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!handle)
        throw std::runtime_error("control_creation_failed");
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return handle;
}
void save_observations() {
    if (app.busy || !app.host_ran || app.test_runtime)
        return;
    try {
        odv::Json result = {{"schema_version", 1},
                            {"source", "user desktop checklist"},
                            {"game_validation", "NOT_RUN"},
                            {"headset_validation", "USER_OBSERVATIONS"}};
        result["owned_depth_preview"] = app.depth_preview;
        result["presentation_mode"] =
            app.depth_preview || !app.stream_channel.empty() ? "STATIC_STEREO_IMAGE_STREAM" : "SYNTHETIC_GEOMETRY";
        const char *names[] = {"depth", "head_tracking", "comfort", "menu_return"};
        for (int i = 0; i < 4; ++i)
            result["checks"][names[i]] =
                SendMessageW(app.visual[i], BM_GETCHECK, 0, 0) == BST_CHECKED ? "PASS" : "NOT_RUN";
        const auto length = GetWindowTextLengthW(app.notes);
        std::wstring notes(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(app.notes, notes.data(), length + 1);
        notes.resize(length);
        result["notes"] = utf8(notes);
        write_json(app.run_directory / L"visual-observations.json", result);
        SetWindowTextW(app.status, L"Your observations were saved with this test's reports.");
    } catch (const std::exception &) {
        SetWindowTextW(app.status, L"Could not save observations. Reports remain available.");
    }
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case mode_id:
            if (!app.busy && HIWORD(wparam) == CBN_SELCHANGE) {
                app.depth_preview = SendMessageW(app.mode, CB_GETCURSEL, 0, 0) == 0;
                app.host_ran = false;
                EnableWindow(app.save, FALSE);
                for (auto box : app.visual)
                    SendMessageW(box, BM_SETCHECK, BST_UNCHECKED, 0);
                SetWindowTextW(app.notes, L"");
                SetWindowTextW(app.status, L"Mode changed. Start a new test before saving observations.");
                EnableWindow(app.visual[1], !app.depth_preview);
                SetWindowTextW(app.detail, app.depth_preview
                                               ? L"Owned courtyard -> matched color/depth -> reconstructed eyes -> GPU "
                                                 L"stream -> OpenXR.\r\nStatic source camera; head tracking is "
                                                 L"unavailable. Crimson Desert is not opened."
                                               : L"Independent synthetic cube views with headset tracking.\r\nThis is "
                                                 L"the original calibration mode, separate from the stream preview.");
            }
            break;
        case start_id:
            operation(true);
            break;
        case doctor_id:
            operation(false);
            break;
        case stop_id:
            if (app.busy) {
                app.worker.request_stop();
                EnableWindow(app.stop, FALSE);
                SetWindowTextW(app.status, L"Stopping safely and saving reports...");
            }
            break;
        case folder_id: {
            const auto path = app.run_directory.empty() ? app.package : app.run_directory;
            ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case save_id:
            save_observations();
            break;
        }
        return 0;
    case WM_APP + 2:
        if (!app.closing && !app.worker.get_stop_token().stop_requested()) {
            if (wparam) {
                SetWindowTextW(app.status, L"Simulated stereo test running. No headset is being tested.");
                SetWindowTextW(app.detail,
                               L"Software runtime verification only. Press Stop to check orderly shutdown.");
                return 0;
            }
            SetWindowTextW(app.status, L"Stereo test running (up to 60 seconds). Put on your headset.");
            SetWindowTextW(
                app.detail,
                app.depth_preview ? L"Owned courtyard through depth reconstruction and the GPU stream.\r\nStatic "
                                    L"camera: head tracking is unavailable. Check depth and image continuity.\r\nThin "
                                    L"black gaps at object edges are unfilled disocclusions. Press Stop to exit."
                : !app.stream_channel.empty()
                    ? L"Receiving the source's left/right images. Source stereo validity is unverified.\r\nGame head "
                      L"tracking is unavailable. Press Stop to exit safely."
                    : L"Look for red, green and blue cubes at different depths. Move your head gently.\r\n"
                      L"If comfortable, open and close the Meta menu and check that the cubes return.\r\nPress "
                      L"Stop at any time to request a safe exit.");
        }
        return 0;
    case finished: {
        if (app.worker.joinable())
            app.worker.join();
        app.busy = false;
        std::lock_guard lock(app.mutex);
        SetWindowTextW(app.status, app.message.c_str());
        SetWindowTextW(app.detail, app.explanation.c_str());
        EnableWindow(app.start, TRUE);
        EnableWindow(app.doctor, TRUE);
        EnableWindow(app.mode, app.stream_channel.empty());
        EnableWindow(app.stop, FALSE);
        EnableWindow(app.save, app.host_ran && !app.test_runtime);
        if (app.closing)
            DestroyWindow(window);
        return 0;
    }
    case WM_CLOSE:
        if (app.busy) {
            app.closing = true;
            app.worker.request_stop();
            EnableWindow(app.stop, FALSE);
            SetWindowTextW(app.status, L"Closing after the test stops and saves its reports...");
        } else
            DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        int argc{};
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            return 2;
        const bool valid = argc == 1 || (argc == 3 && std::wstring_view(argv[1]) == L"--stereo-stream" && argv[2][0]);
        if (valid && argc == 3)
            app.stream_channel = argv[2];
        LocalFree(argv);
        if (!valid)
            return 2;
        const bool streaming = !app.stream_channel.empty();
        app.depth_preview = !streaming;
        wchar_t module[32768]{};
        const auto size = GetModuleFileNameW(nullptr, module, 32768);
        if (!size || size >= 32768)
            return 1;
        app.package = std::filesystem::path(module).parent_path();
        app.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        app.heading = CreateFontW(-27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        WNDCLASSW wc{};
        wc.lpfnWndProc = window_proc;
        wc.hInstance = instance;
        wc.lpszClassName = L"OpenDesertVRDesktop";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        if (!RegisterClassW(&wc))
            return 1;
        RECT rect{0, 0, 780, 620};
        AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        app.window =
            CreateWindowW(wc.lpszClassName, L"OpenDesertVR - Headset Test",
                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                          rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        if (!app.window)
            return 1;
        auto title = control(L"STATIC", L"OpenDesertVR", 0, 26, 18, 730, 40);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(app.heading), TRUE);
        control(L"STATIC", L"Quest Pro PCVR stereo testing", 0, 28, 62, 360, 24);
        app.mode = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 390, 58, 348, 130, mode_id);
        SendMessageW(app.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Offline depth stereo - owned courtyard"));
        SendMessageW(app.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Original tracked cube calibration"));
        if (streaming)
            SendMessageW(app.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"External stereo stream"));
        SendMessageW(app.mode, CB_SETCURSEL, streaming ? 2 : 0, 0);
        EnableWindow(app.mode, !streaming);
        control(
            L"STATIC",
            streaming
                ? L"Receiving independent left/right images from a running source.\r\nSource camera validity is "
                  L"unverified. Game head tracking is not implemented.\r\nThe viewer stops if the source disconnects."
                : L"1. Connect PCVR and wake the headset; use your active OpenXR runtime.\r\n2. Choose a mode and "
                  L"Start. Courtyard: static camera. Cube calibration: head tracking.\r\n3. Save observations. This "
                  L"test does not open Crimson Desert.",
            0, 28, 98, 720, 72);
        app.start =
            control(L"BUTTON", L"Start 60-second test", WS_TABSTOP | BS_DEFPUSHBUTTON, 28, 185, 210, 40, start_id);
        app.doctor = control(L"BUTTON", L"Check readiness", WS_TABSTOP, 250, 185, 165, 40, doctor_id);
        app.stop = control(L"BUTTON", L"Stop", WS_TABSTOP, 427, 185, 100, 40, stop_id);
        app.folder = control(L"BUTTON", L"Open reports", WS_TABSTOP, 539, 185, 185, 40, folder_id);
        EnableWindow(app.stop, FALSE);
        EnableWindow(app.folder, FALSE);
        app.status = control(L"STATIC", L"Ready when you are. No test has started.", 0, 28, 244, 710, 44);
        app.detail = control(
            L"EDIT",
            streaming
                ? L"Stream delivery and source stereo validity are reported separately.\r\nCrimson "
                  L"Desert stereo remains unverified."
                : L"Owned courtyard -> matched color/depth -> reconstructed eyes -> GPU stream -> OpenXR.\r\nThis is "
                  L"depth-reconstructed stereo, not native game stereo. Black edge gaps may appear.",
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 28, 292, 710, 105);
        control(L"STATIC", L"After testing, tick only what you personally checked:", 0, 28, 411, 710, 23);
        const wchar_t *labels[] = {L"Correct depth", L"Head tracking", L"Comfortable", L"Meta menu return"};
        for (int i = 0; i < 4; ++i)
            app.visual[i] =
                control(L"BUTTON", labels[i], BS_AUTOCHECKBOX | WS_TABSTOP, 28 + i * 177, 439, 177, 27, 200 + i);
        EnableWindow(app.visual[1], FALSE);
        control(L"STATIC", L"Notes (optional):", 0, 28, 486, 128, 25);
        app.notes = control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 160, 481, 378, 30);
        SendMessageW(app.notes, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Optional notes: blur, freezing, missing cubes..."));
        SendMessageW(app.notes, EM_SETLIMITTEXT, 2000, 0);
        app.save = control(L"BUTTON", L"Save observations", WS_TABSTOP, 550, 479, 188, 34, save_id);
        EnableWindow(app.save, FALSE);
        control(L"STATIC",
                L"Reports stay on this PC. Unchecked observations remain NOT RUN.\r\nStop requests an orderly runtime "
                L"exit; please wait for reports to finish saving.",
                0, 28, 541, 710, 52);
        ShowWindow(app.window, show);
        UpdateWindow(app.window);
        if (streaming)
            PostMessageW(app.window, WM_COMMAND, start_id, 0);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(app.window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        DeleteObject(app.font);
        DeleteObject(app.heading);
        return 0;
    } catch (const std::exception &) {
        MessageBoxW(nullptr, L"The testing app could not start. Extract the complete package into a writable folder.",
                    L"OpenDesertVR", MB_OK | MB_ICONERROR);
        return 1;
    }
}
