# M0 architecture

odv_math owns right-handed meters (+X right, +Y up, -Z forward), row-major
matrices multiplying column vectors and D3D depth [0,1].
odv_stereo builds three colored cubes at 1, 3 and 8 meters in LOCAL space.
Each eye's OpenXR pose and asymmetric FOV independently transforms world
vertices to homogeneous clip coordinates on the CPU. D3D12 rasterizes each
eye into its own runtime swapchain with a depth buffer. No mono image is copied.

odv_core owns typed failure categories, bounded structured logs, a strict
version-1 JSON configuration and diagnostic checks. odv_openxr owns instance,
system, session, spaces, events, views and swapchains. IXrFrame makes the
wait/begin/locate/eyes/end protocol testable without a runtime.
odv_d3d12 owns COM graphics objects, matched adapter, queue and fence.
odv_game_crimson implements only the inactive IGameAdapter capability report;
the XR host has no game imports.

Ownership: image handles/textures belong to OpenXR. Application depth/upload
resources belong to Renderer. The host drains the GPU before destroying
swapchains, reference space, session, then instance. COM resources follow.
Per-eye rendering is intentionally serialized with CPU fence waits: reference
correctness first, throughput tuning later. See ADR 0001.

Default run lasts 60 seconds; Ctrl+C requests exit through OpenXR. The runtime
must transition to STOPPING before xrEndSession. Session/instance loss ends the
run and requires a fresh process. No automatic reconnection or game interaction.

Reports export stable check codes, booleans, counts, timings, transforms and
FOVs, not runtime paths, adapter LUIDs, serials or external error text.
The local console may contain upstream runtime/loader messages; review those
before sharing. Reports are opt-in local files, never uploaded.
