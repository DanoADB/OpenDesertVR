# Static stereo-pipeline audit

Reviewed 2026-11-04 for backlog issue #10. **No upstream mod was built, run,
or adopted.** This is read-only architecture preparation; M0 hardware
acceptance and all Crimson Desert integration gates remain pending.

## Decision

Retain the original standalone Khronos/D3D12 host. Cyberpunk VR's inspected
source attempts alternate-eye camera updates and copies presented frames,
but does not establish a coordinated, tested two-camera world-render pair.
REFramework contains engine-level matrix overrides and an additional render
chain, making it a useful architecture reference. Its RE Engine-specific
mechanisms do not establish access to BlackSpace. Both upstream runtime and
image-correctness claims remain **UNVERIFIED** in this review.

## Immutable review inputs

| Repository | Reviewed revision | Root license |
|---|---|---|
| pinducoding/Cyberpunk2077-VR-OpenSource | `222d9061b02cce6a90123c51950900610a086400` | [MIT](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/LICENSE) |
| praydog/REFramework | `f00d6bee503125180621a6666779499f9a30881d` | [MIT](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/LICENSE) |

[Machine-readable inventory](stereo-source-inventory.json) records source URLs,
SHA256 fingerprints of files read, selected dependency pins and license texts.
Downloaded reference source stays under ignored `out/research/` and is not
redistributed in the application or package.

## Cyberpunk VR: trace the actual data path

| Question | Source observation | Evidence |
|---|---|---|
| Where does headset pose enter? | `Update` waits/begins an XR frame, locates views, then converts **view 0** into the returned pose. This is not evidence of a game projection override. | [VRSystem.cpp:957–1007](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/VRSystem.cpp#L957-L1007) |
| Does the preferred camera path apply it? | SDK discovery returns success without installing the fallback update hook. The SDK transform-write block remains a TODO; subsequent code caches a pose. | [CameraHook.cpp:34–45](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/CameraHook.cpp#L34-L45), [185–235](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/CameraHook.cpp#L185-L235) |
| Is there any game-camera attempt? | The fallback callback writes a component's world position/orientation with an alternating offset, then calls the original update. That differs from the incomplete SDK path and still needs execution/camera-timing proof. | [CameraHook.cpp:237–284](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/CameraHook.cpp#L237-L284) |
| What reaches the XR images? | Present selects the current backbuffer, advances its own eye counter and passes that texture to submission. | [D3D12Hook.cpp:125–147](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/D3D12Hook.cpp#L125-L147) |
| How are images submitted? | Submission acquires/waits an eye image and copies the game texture into it. The right-eye branch ends the XR frame with two projection views. | [VRSystem.cpp:1024–1092](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/VRSystem.cpp#L1024-L1092) |
| Is the copy a second world render? | The copy routine records resource transitions and `CopyTextureRegion`; it does not request a world render. | [VRSystem.cpp:677–735](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/VRSystem.cpp#L677-L735) |

Static search of all repository `src/` files and the supplied Lua entry point
found `UpdateVRCamera` only as a declaration/definition, not an invocation.
The camera callback, cached-pose routine and Present path use separate counters.
**Inference:** their parity alone does not prove that captured images were
rendered using the intended eye pose and projection. A fallback camera write
could affect game rendering if invoked at a suitable engine stage; this review
does not demonstrate that execution.

The inspected path does not explicitly schedule an additional world render.
It alternates capture across presented frames. It must not be described as
a verified, temporally coherent stereo pair or automatically condemned as duplicated mono:
actual render-camera provenance is missing. The copy queue is independently
created from the swapchain device; producer-queue synchronization was not
validated here. [Queue creation](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/src/D3D12Hook.cpp#L74-L105)

## REFramework: engine rendering precedes the copy

| Stage | Source observation | Evidence |
|---|---|---|
| Runtime matrices | OpenXR eye poses and asymmetric FOV populate per-eye matrices. | [OpenXR.cpp:244–264](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/src/mods/vr/runtimes/OpenXR.cpp#L244-L264) |
| Engine connection | Camera-get hooks call the engine method and dispatch resulting view/projection matrices to mod callbacks. | [Hooks.cpp:1029–1078](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/src/mods/Hooks.cpp#L1029-L1078) |
| Per-eye modification | VR callbacks replace the camera projection and compose the selected eye transform into the primary camera view. Eye selection is tied to frame parity. | [VR.cpp:233–309](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/src/mods/VR.cpp#L233-L309), [2168–2198](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/src/mods/VR.cpp#L2168-L2198) |
| Render scheduling | With AFR disabled, the end-render callback runs a filtered engine render chain again for the other eye. AFR/recursive branches return without scheduling that extra chain. | [VR.cpp:3085–3199](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/src/mods/VR.cpp#L3085-L3199) |
| Capture and submit | D3D12 captures the resulting backbuffer into the selected eye image, then ends the XR frame on the right-eye interval. | [D3D12Component.cpp:59–155](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/src/mods/vr/D3D12Component.cpp#L59-L155) |
| Composition metadata | XR projection views use stored poses/FOV and the selected swapchains at predicted display time. | [OpenXR.cpp:1290–1348](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/src/mods/vr/runtimes/OpenXR.cpp#L1290-L1348) |

**Inference from scheduling code:** non-AFR mode is designed to produce two
engine rendering passes for a stereo pair; AFR uses successive game frames.
This is not a measured count of GPU world passes or a hardware test. The
second chain removes selected simulation/effect stages, revealing engine
coupling and temporal-state concerns. No such scheduling mechanism has been
established for Crimson Desert.

The backbuffer copy is therefore only the transport stage in the inspected
REFramework design. The camera callbacks and render scheduler are essential
to its stereo explanation. Copying that transport layer alone would omit the
parts that distinguish the eye renders.

## Dependency and license review

The root MIT licenses do not clear entire dependency trees for redistribution.
The inventory covers root licenses and the following declared direct inputs;
no dependency below was added to OpenDesertVR.

| Project input | Reviewed license result | Pin/evidence |
|---|---|---|
| Cyberpunk RED4ext.SDK | MIT; separate D3D12MemoryAllocator notice inspected | `a0776d41359fe1ba01705fd97835018c56aee69b`, [SDK license](https://github.com/WopsS/RED4ext.SDK/blob/a0776d41359fe1ba01705fd97835018c56aee69b/LICENSE.md) |
| Cyberpunk OpenXR SDK | Apache-2.0 root; further per-file/transitive audit needed before adoption | `c15d38cb4bb10a5b7e075f74493ff13896e2597a`; [.gitmodules](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/.gitmodules), [build](https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource/blob/222d9061b02cce6a90123c51950900610a086400/CMakeLists.txt#L13-L19) |
| REFramework asmjit | Zlib | `2a706fd2ba355808cada31ac1eed8ce28caa6b37` |
| REFramework OpenXR | Apache-2.0 root; per-file/transitive audit still needed | `458984d7f59d1ae6dc1b597d94b02e4f7132eaba` |
| DirectXTK / DirectXTK12 | MIT | `f5026eb34e7053b1aff325d38db107703f394974` / `be5dfc7e391aefaa4eeab8e8a08f7e2669e56cbb` |
| safetyhook / kananlib | Boost Software License 1.0 | `b046e123dc69821f2c375161e0adef3c6d9c9db4` / `43cb5353615dcbc5d4945b1b9ab685b11e8b0d37` |
| FreeType | Choice of FTL or GPL-2.0-or-later, plus component notices | `f1cd6dbfa0c98f352b698448f40ac27e8fb3832e` |
| bddisasm | Apache-2.0 | `70db095765ab2066dd88dfb7bbcc42259ed167c5` |
| GLM / ImGui | MIT option / MIT | Gitlink revisions in the inventory |
| MinHook | BSD-2-Clause text including embedded HDE notices | `98b74f1fc12d00313d91f10450e5b3e0036175e3` |
| spdlog | MIT, with separate fmt notice requirement | `7c02e204c92545f869e2f04edaab1f19fe8b19fd` |

REFramework's fetched dependency declarations are pinned in
[CMakeLists.txt:87–157](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/CMakeLists.txt#L87-L157); additional gitlinks are
listed in [.gitmodules](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/.gitmodules). The inventory links each selected
license to the exact dependency revision. Bundled OpenVR libraries,
fonts/resources, remaining vendored code and full transitive dependencies
are **not cleared for redistribution** by this review. In particular,
[OpenVR library copying](https://github.com/praydog/REFramework/blob/f00d6bee503125180621a6666779499f9a30881d/CMakeLists.txt#L312-L326) requires its own
provenance/license audit before reuse.

## Consequences for OpenDesertVR

- Keep M0's independent synthetic renderer and official loader, with no new
  game hooks or borrowed mod dependencies.
- Correlate eye diagnostics with one frame index and predicted display-time
  offset. Separate eye labels or Present parity are insufficient provenance.
- Require a future approved engine adapter to connect each world-render
  camera/projection to its captured image and XR submission. Document whether
  the pair shares one simulation step or spans two game frames.
- Record temporal-history, HUD/effect, producer-queue and frame-ownership
  experiments at the relevant gates; evidence from another engine cannot
  supply missing BlackSpace capabilities.

The M0 logging change prompted by this audit adds frame index and relative
predicted display timing to each sampled eye event. It exposes no absolute
runtime clock identifier and does not alter rendering or game capability
claims. Hardware acceptance is still required before G1.
