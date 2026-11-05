// SPDX-License-Identifier: MIT
// Run test children at medium integrity without administrator group privileges.
// The OpenXR loader intentionally rejects runtime environment overrides at high
// integrity. Do not change that upstream protection or machine registration.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

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
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc < 2)
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
        const bool reduce = level > SECURITY_MANDATORY_MEDIUM_RID;
        if (reduce) {
            SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
            Sid admin;
            checked(AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                             0, 0, &admin.value),
                    "administrator SID");
            SID_AND_ATTRIBUTES disable{admin.value, 0};
            checked(CreateRestrictedToken(token.value, DISABLE_MAX_PRIVILEGE, 1, &disable, 0, nullptr, 0, nullptr,
                                          &restricted.value),
                    "restrict test privileges");
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
        }
        std::wstring command;
        for (int i = 1; i < argc; ++i) {
            if (i > 1)
                command += L' ';
            command += quote(argv[i]);
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process{};
        if (reduce)
            checked(CreateProcessAsUserW(restricted.value, nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                         nullptr, &startup, &process),
                    "start reduced-privilege test");
        else
            checked(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                                   &process),
                    "start test");
        Handle child{process.hProcess}, thread{process.hThread};
        if (WaitForSingleObject(child.value, INFINITE) != WAIT_OBJECT_0)
            throw std::runtime_error("test process wait failed");
        DWORD result = 1;
        checked(GetExitCodeProcess(child.value, &result), "test exit code");
        return static_cast<int>(result);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 127;
    }
}
