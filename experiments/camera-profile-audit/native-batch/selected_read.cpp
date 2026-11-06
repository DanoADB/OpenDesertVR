// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "acquire.h"
#include <windows.h>

namespace {
struct NativeReader {
    HANDLE process;
    ULONGLONG started{GetTickCount64()};
    bool expired() const noexcept { return GetTickCount64() - started > 1000; }
    bool read(std::uint64_t address, void *destination, std::uint32_t size, std::uint32_t alignment) const noexcept {
        if (!size || size > 128 || address < 65536 || address > 0x7fffffffffffULL - size || address % alignment ||
            WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
            return false;
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQueryEx(process, reinterpret_cast<void *>(address), &info, sizeof(info)) != sizeof(info))
            return false;
        auto protection = info.Protect & 0xff;
        auto base = reinterpret_cast<std::uint64_t>(info.BaseAddress);
        if (info.State != MEM_COMMIT || info.Protect & PAGE_GUARD ||
            (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY) ||
            address < base || base > UINT64_MAX - info.RegionSize || address + size > base + info.RegionSize)
            return false;
        SIZE_T copied{};
        return ReadProcessMemory(process, reinterpret_cast<void *>(address), destination, size, &copied) &&
               copied == size && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
};
} // namespace

extern "C" void odv_read_selected_v1(void *process, std::uint64_t global, std::uint64_t table,
                                     std::uint32_t source_offset, odv::research::Result *result) noexcept {
    if (!result)
        return;
    if (!process || process == INVALID_HANDLE_VALUE) {
        *result = {};
        result->status = odv::research::Status::invalid_arguments;
        return;
    }
    LARGE_INTEGER started{}, finished{}, frequency{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    NativeReader reader{process};
    odv::research::acquire(reader, global, table, source_offset, *result);
    QueryPerformanceCounter(&finished);
    if (frequency.QuadPart > 0 && finished.QuadPart >= started.QuadPart)
        result->elapsed_microseconds =
            static_cast<std::uint64_t>((finished.QuadPart - started.QuadPart) * 1000000 / frequency.QuadPart);
}
