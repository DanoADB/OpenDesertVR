# ADR 0001: independent, serialized OpenXR/D3D12 testhost

Accepted for M0. Use official pinned Khronos loader and a native C++20 host.
Keep game capabilities behind an inactive adapter and independent of the host.

Use runtime-required LUID/feature level; never substitute the default adapter.
Use separate eye swapchains, runtime poses/FOV at predicted display time, LOCAL
space and D3D12 RENDER_TARGET image state prescribed by OpenXR. Application
depth stays DEPTH_WRITE; upload stays GENERIC_READ. No PRESENT/COMMON barrier
is applied to runtime images. Fence GPU completion before releasing an image
or resetting/reusing resources. This is deliberately conservative and can
reduce performance; report CPU frame timing honestly.

Per-eye CPU vertex transforms simplify the tiny calibration renderer and make
the exact geometry path testable. Full game rendering would require independent
engine views and cannot reuse this as an injection mechanism.
Session loss tears down the run rather than attempting untested recovery.
