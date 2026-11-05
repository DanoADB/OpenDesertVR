// SPDX-License-Identifier: MIT
// Run test children at medium integrity without administrator group privileges.
// The OpenXR loader intentionally rejects runtime environment overrides at high
// integrity. Do not change that upstream protection or machine registration.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <sddl.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Handle {
    HANDLE value{};
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
};
struct Sid {
    PSID value{};
    ~Sid() {
        if (value)
            FreeSid(value);
    }
};
void checked(BOOL result, const char *operation) {
    if (!result)
        throw std::runtime_error(std::string(operation) + " failed, Win32=" + std::to_string(GetLastError()));
}
std::wstring quote(const std::wstring &arg) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result += c;
        } else {
            result.append(slashes, L'\\');
            result += c;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}
struct TestDesktop {
    HWINSTA station{};
    HDESK desktop{};
    std::wstring path;
    ~TestDesktop() {
        if (desktop)
            CloseDesktop(desktop);
        if (station)
            CloseWindowStation(station);
    }
    void create(HANDLE token) {
        // Hosted service desktops can grant access only through Administrators.
        // Create private, medium-integrity test objects granting the current user
        // access. Never alter the active desktop/window station's permissions.
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> bytes(size);
        checked(GetTokenInformation(token, TokenUser, bytes.data(), size, &size), "test desktop user");
        auto user = reinterpret_cast<TOKEN_USER *>(bytes.data());
        LPWSTR sid_string{};
        checked(ConvertSidToStringSidW(user->User.Sid, &sid_string), "test desktop SID text");
        const std::wstring sddl = L"D:(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid_string) + L")S:(ML;;NW;;;ME)";
        LocalFree(sid_string);
        PSECURITY_DESCRIPTOR security{};
        checked(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &security, nullptr),
                "test desktop security");
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security, FALSE};
        const auto name =
            L"odv-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        const auto original = GetProcessWindowStation();
        const auto original_desktop = GetThreadDesktop(GetCurrentThreadId());
        station = CreateWindowStationW(name.c_str(), CWF_CREATE_ONLY, WINSTA_ALL_ACCESS, &attributes);
        const auto error = GetLastError();
        if (!station) {
            LocalFree(security);
            SetLastError(error);
            checked(FALSE, "private test window station");
        }
        checked(SetProcessWindowStation(station), "select private test window station");
        desktop = CreateDesktopW(L"default", nullptr, nullptr, 0, GENERIC_ALL, &attributes);
        const auto desktop_error = GetLastError();
        const BOOL restored = SetProcessWindowStation(original);
        const BOOL thread_restored = SetThreadDesktop(original_desktop);
        LocalFree(security);
        checked(restored, "restore test window station");
        checked(thread_restored, "restore test thread desktop");
        if (!desktop) {
            SetLastError(desktop_error);
            checked(FALSE, "private test desktop");
        }
        path = name + L"\\default";
        std::cerr << "test launcher: private medium-integrity desktop ready\n" << std::flush;
    }
};
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc < 2)
        return 2;
    const bool force_restricted = std::wstring(argv[1]) == L"--restricted";
    const int first = force_restricted ? 2 : 1;
    if (argc <= first)
        return 2;
    try {
        Handle token, restricted;
        checked(OpenProcessToken(GetCurrentProcess(),
                                 TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
                                 &token.value),
                "open test token");
        DWORD size = 0;
        GetTokenInformation(token.value, TokenIntegrityLevel, nullptr, 0, &size);
        std::vector<unsigned char> storage(size);
        checked(GetTokenInformation(token.value, TokenIntegrityLevel, storage.data(), size, &size),
                "read test integrity");
        const auto label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(storage.data());
        const DWORD level = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
        const bool reduce = force_restricted || level > SECURITY_MANDATORY_MEDIUM_RID;
        TestDesktop test_desktop;
        if (reduce) {
            std::cerr << "test launcher: reducing elevated parent token\n" << std::flush;
            SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
            Sid admin;
            checked(AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                             0, 0, &admin.value),
                    "administrator SID");
            SID_AND_ATTRIBUTES disable{admin.value, 0};
            checked(CreateRestrictedToken(token.value, DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 1, &disable, 0, nullptr, 0,
                                          nullptr, &restricted.value),
                    "restrict test privileges");
            std::cerr << "test launcher: restricted token created\n" << std::flush;
            SID_IDENTIFIER_AUTHORITY mandatory = SECURITY_MANDATORY_LABEL_AUTHORITY;
            Sid medium;
            checked(AllocateAndInitializeSid(&mandatory, 1, SECURITY_MANDATORY_MEDIUM_RID, 0, 0, 0, 0, 0, 0, 0,
                                             &medium.value),
                    "medium integrity SID");
            TOKEN_MANDATORY_LABEL target{};
            target.Label.Sid = medium.value;
            target.Label.Attributes = SE_GROUP_INTEGRITY;
            checked(SetTokenInformation(restricted.value, TokenIntegrityLevel, &target,
                                        sizeof(target) + GetLengthSid(medium.value)),
                    "lower test integrity");
            std::cerr << "test launcher: medium integrity token ready\n" << std::flush;
            if (level > SECURITY_MANDATORY_MEDIUM_RID)
                test_desktop.create(token.value);
        }
        std::wstring command;
        for (int i = first; i < argc; ++i) {
            if (i > first)
                command += L' ';
            command += quote(argv[i]);
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (!test_desktop.path.empty())
            startup.lpDesktop = test_desktop.path.data();
        PROCESS_INFORMATION process{};
        if (reduce)
            checked(CreateProcessAsUserW(restricted.value, nullptr, command.data(), nullptr, nullptr, TRUE,
                                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process),
                    "start reduced-privilege test");
        else
            checked(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                                   &process),
                    "start test");
        Handle child{process.hProcess}, thread{process.hThread};
        if (reduce)
            std::cerr << "test launcher: child started\n" << std::flush;
        if (WaitForSingleObject(child.value, INFINITE) != WAIT_OBJECT_0)
            throw std::runtime_error("test process wait failed");
        DWORD result = 1;
        checked(GetExitCodeProcess(child.value, &result), "test exit code");
        if (result != 0)
            std::cerr << "test launcher: child exit=0x" << std::hex << result << '\n';
        return static_cast<int>(result);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 127;
    }
}
