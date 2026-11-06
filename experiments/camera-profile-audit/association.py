# SPDX-License-Identifier: MIT
"""Minimal pointer association observation; no camera field reads or writes."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import time


class ObservationError(RuntimeError):
    pass


def sample_association(read_pointer, global_address, expected_table, pause=lambda: None):
    """Three bracketed samples, at most twelve pointer reads. Fail on conflict."""
    previous = None
    for _ in range(3):
        first = read_pointer(global_address)
        if not first:
            raise ObservationError("global_unavailable")
        one = read_pointer(first)
        two = read_pointer(first)
        last = read_pointer(global_address)
        if first != last or one != two:
            raise ObservationError("source_changed_during_sample")
        if one != expected_table:
            raise ObservationError("vtable_mismatch")
        if previous is not None and first != previous:
            raise ObservationError("source_replaced_between_samples")
        previous = first
        pause()
    return {"status": "OBSERVED", "stable_bracketed_samples": 3,
            "pointer_reads": 12, "bytes_read": 96,
            "scope": "Global/object first-pointer association only; camera semantics, fields, activity, pose and control remain unverified."}


class MemoryInfo(ctypes.Structure):
    _fields_ = [("base", ctypes.c_uint64), ("allocation", ctypes.c_uint64),
                ("allocation_protect", wintypes.DWORD), ("padding1", wintypes.DWORD),
                ("size", ctypes.c_uint64), ("state", wintypes.DWORD),
                ("protect", wintypes.DWORD), ("kind", wintypes.DWORD), ("padding2", wintypes.DWORD)]


class ModuleInfo(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("size", wintypes.DWORD), ("entry", ctypes.c_void_p)]


class ReadOnlyProcess:
    # VirtualQueryEx requires QUERY_INFORMATION. No write, operation, thread,
    # termination, token-adjustment or debug privilege is requested.
    rights = 0x00100410  # SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ

    def __init__(self, pid, executable):
        if os.name != "nt" or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise ObservationError("requires_windows_x64")
        if type(pid) is not int or not 0 < pid <= 0xffffffff:
            raise ObservationError("invalid_process_id")
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.handle = None
        definitions = {
            "OpenProcess": ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
            "WaitForSingleObject": ([wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
            "QueryFullProcessImageNameW": ([wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                            ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            "K32EnumProcessModulesEx": ([wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p), wintypes.DWORD,
                                         ctypes.POINTER(wintypes.DWORD), wintypes.DWORD], wintypes.BOOL),
            "K32GetModuleFileNameExW": ([wintypes.HANDLE, ctypes.c_void_p, wintypes.LPWSTR, wintypes.DWORD], wintypes.DWORD),
            "K32GetModuleInformation": ([wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(ModuleInfo), wintypes.DWORD], wintypes.BOOL),
            "VirtualQueryEx": ([wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(MemoryInfo), ctypes.c_size_t], ctypes.c_size_t),
            "ReadProcessMemory": ([wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                    ctypes.POINTER(ctypes.c_size_t)], wintypes.BOOL),
        }
        for name, (arguments, result) in definitions.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = arguments, result
        try:
            self.handle = self.api.OpenProcess(self.rights, False, pid)
            if not self.handle:
                raise ObservationError("process_access_denied_or_unavailable")
            self.active()
            path = ctypes.create_unicode_buffer(32768)
            length = wintypes.DWORD(len(path))
            if not self.api.QueryFullProcessImageNameW(self.handle, 0, path, ctypes.byref(length)):
                raise ObservationError("process_identity_query_failed")
            expected = os.path.normcase(str(Path(executable).resolve()))
            if os.path.normcase(str(Path(path.value).resolve())) != expected:
                raise ObservationError("process_path_mismatch")
            modules = (ctypes.c_void_p * 512)()
            needed = wintypes.DWORD()
            if (not self.api.K32EnumProcessModulesEx(self.handle, modules, ctypes.sizeof(modules),
                                                    ctypes.byref(needed), 2) or
                    not needed.value or needed.value > ctypes.sizeof(modules) or needed.value % 8):
                raise ObservationError("module_enumeration_failed")
            matches = []
            for module in modules[:needed.value // 8]:
                count = self.api.K32GetModuleFileNameExW(self.handle, module, path, len(path))
                if not count or count >= len(path):
                    raise ObservationError("module_identity_query_failed")
                if os.path.normcase(str(Path(path.value).resolve())) == expected:
                    info = ModuleInfo()
                    if not self.api.K32GetModuleInformation(self.handle, module, ctypes.byref(info), ctypes.sizeof(info)):
                        raise ObservationError("module_information_failed")
                    matches.append((int(info.base), int(info.size)))
            if len(matches) != 1:
                raise ObservationError("main_module_not_unique")
            self.base, self.image_size = matches[0]
            self.active()
        except Exception:
            self.close()
            raise

    def active(self):
        if not self.handle or self.api.WaitForSingleObject(self.handle, 0) != 258:
            raise ObservationError("target_not_live")

    def region(self, address, image_base=None, writable=None):
        self.active()
        if type(address) is not int or address < 65536 or address > 0x7fffffffffff - 8 or address % 8:
            raise ObservationError("invalid_pointer_range")
        info = MemoryInfo()
        if self.api.VirtualQueryEx(self.handle, address, ctypes.byref(info), ctypes.sizeof(info)) != ctypes.sizeof(info):
            raise ObservationError("memory_query_failed")
        protection = info.protect & 0xff
        if (info.state != 0x1000 or info.protect & 0x100 or protection not in {2, 4, 8} or
                not info.base <= address or address + 8 > info.base + info.size):
            raise ObservationError("memory_not_readable_nonexecutable")
        if image_base is not None and (info.kind != 0x1000000 or info.allocation != image_base):
            raise ObservationError("memory_not_expected_image")
        if writable is True and protection not in {4, 8}:
            raise ObservationError("global_not_writable")
        if writable is False and protection != 2:
            raise ObservationError("table_not_readonly")

    def read_pointer(self, address):
        self.region(address)
        value, read = ctypes.c_uint64(), ctypes.c_size_t()
        if (not self.api.ReadProcessMemory(self.handle, address, ctypes.byref(value), 8, ctypes.byref(read)) or
                read.value != 8):
            raise ObservationError("pointer_read_failed")
        self.active()
        return value.value

    def close(self):
        if self.handle:
            if not self.api.CloseHandle(self.handle):
                raise ObservationError("process_handle_close_failed")
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def observe_process(reader, global_rva, table_rva, image_size):
    if reader.image_size != image_size:
        raise ObservationError("loaded_image_size_mismatch")
    global_address, table_address = reader.base + global_rva, reader.base + table_rva
    reader.region(global_address, image_base=reader.base, writable=True)
    reader.region(table_address, image_base=reader.base, writable=False)
    deadline = time.monotonic() + 1.0

    def read(address):
        if time.monotonic() > deadline:
            raise ObservationError("sampling_deadline_expired")
        return reader.read_pointer(address)

    return sample_association(read, global_address, table_address, pause=lambda: time.sleep(0.05))
