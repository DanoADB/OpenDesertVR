# SPDX-License-Identifier: MIT
"""Bounded stdio client for an explicitly selected owned Devkit bridge."""
import json
import queue
import re
import subprocess
import threading

MAX_LINE = 1024 * 1024
TOOLS = {"devkit_status", "devkit_select_device", "devkit_queue_snapshot", "devkit_snapshot_summary",
         "devkit_list_draws", "devkit_get_draw", "devkit_get_shader"}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate_response_key")
        result[key] = value
    return result


def decode_response(data, expected_id):
    if not data or len(data) > MAX_LINE or not data.endswith(b"\n"):
        raise ValueError("response_line_outside_contract")
    value = json.loads(data.decode("utf-8", errors="strict"), object_pairs_hook=unique_object,
                       parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite_response")))
    if (type(value) is not dict or value.get("jsonrpc") != "2.0" or
            type(value.get("id")) is not int or value["id"] != expected_id or
            "error" in value or "result" not in value):
        raise ValueError("response_identity_or_result_rejected")
    return value["result"]


def tool_arguments(name, arguments):
    if name not in TOOLS or type(arguments) is not dict:
        raise ValueError("unapproved_observer_operation")
    result = dict(arguments)
    allowed = {"deviceIndex"}
    if name == "devkit_get_draw":
        allowed.add("drawIndex")
        if type(result.get("drawIndex")) is not int or not 0 <= result["drawIndex"] <= 4096:
            raise ValueError("invalid_draw_index")
    if name == "devkit_list_draws":
        allowed.update(("offset", "limit"))
        if type(result.get("limit")) is not int or not 1 <= result["limit"] <= 32:
            raise ValueError("invalid_draw_page")
        if type(result.get("offset", 0)) is not int or not 0 <= result.get("offset", 0) <= 4096:
            raise ValueError("invalid_draw_offset")
    if name == "devkit_get_shader":
        allowed.update(("shaderHash", "view"))
        if type(result.get("shaderHash")) is not str or not re.fullmatch(r"0x[0-9a-fA-F]{8}", result["shaderHash"]):
            raise ValueError("invalid_shader_hash")
        if result.get("view", "summary") != "summary":
            raise ValueError("shader_text_not_requested")
        result["view"] = "summary"
    if set(result) - allowed:
        raise ValueError("unknown_observer_arguments")
    if name != "devkit_status" or "deviceIndex" in result:
        if type(result.get("deviceIndex")) is not int or not 0 <= result["deviceIndex"] <= 15:
            raise ValueError("explicit_device_required")
    return result


class Bridge:
    def __init__(self, executable, pipe_name, stderr):
        if not re.fullmatch(r"renodx-devkit-mcp-[1-9][0-9]{0,9}", pipe_name):
            raise ValueError("explicit_owned_pipe_required")
        self.process = subprocess.Popen([str(executable), "--pipe", pipe_name, "--wait-ms", "1000"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr,
                                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.messages = queue.Queue(maxsize=2)
        self.next_id = 0
        self.calls = 0

        def reader():
            try:
                while True:
                    data = self.process.stdout.readline(MAX_LINE + 1)
                    self.messages.put_nowait(data)
                    if not data or len(data) > MAX_LINE:
                        break
            except Exception:
                try:
                    self.messages.put_nowait(b"")
                except queue.Full:
                    pass

        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()

    def request(self, method, params):
        if method not in ("initialize", "tools/call") or self.calls >= 32:
            raise ValueError("observer_request_budget_or_method")
        if method == "tools/call":
            if type(params) is not dict or set(params) != {"name", "arguments"}:
                raise ValueError("invalid_tool_request")
            params = {"name": params["name"], "arguments": tool_arguments(params["name"], params["arguments"])}
        self.calls += 1
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params}
        payload = json.dumps(message, allow_nan=False).encode() + b"\n"
        if len(payload) > 4096:
            raise ValueError("observer_request_size")
        self.process.stdin.write(payload)
        self.process.stdin.flush()
        result = decode_response(self.messages.get(timeout=10), self.next_id)
        return result

    def initialize(self):
        result = self.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                             "clientInfo": {"name": "odv-owned-qualification", "version": "1"}})
        if (type(result) is not dict or result.get("protocolVersion") not in ("2025-11-25", "2025-06-18") or
                type(result.get("serverInfo")) is not dict):
            raise ValueError("bridge_initialize_rejected")
        self.process.stdin.write(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
        self.process.stdin.flush()

    def tool(self, name, arguments):
        arguments = tool_arguments(name, arguments)
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        if (type(result) is not dict or result.get("isError") is not False or
                type(result.get("structuredContent")) is not dict):
            raise ValueError("observer_tool_result_rejected")
        return result["structuredContent"]

    def close(self):
        if self.process.stdin and not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()  # Only this controller's bridge process.
            self.process.wait(timeout=5)
        self.process.stdout.close()
        self.reader.join(timeout=1)
        return self.process.returncode
