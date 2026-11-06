# Licenses and dependency audit

New M0 implementation files are MIT (LICENSE-MIT). The initial repository was
CC0 (LICENSE); its original math and documentation retain that dedication.
No existing ownership is relicensed by this change. See ADR 0002.

| Dependency | Immutable source revision | License | Used files |
|---|---|---|---|
| KhronosGroup/OpenXR-SDK 1.1.43 | 781f2eab3698d653c804ecbd11e0aed47eaad1c6 | Apache-2.0, per-file notices | official headers, loader, common dispatch/platform helpers |
| JsonCpp bundled inside that SDK | same SDK revision | MIT/public-domain option | src/external/jsoncpp sources built into loader |
| nlohmann/json 3.11.3 | 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03 | MIT | include/nlohmann headers |

CMake downloads revision-specific archives and verifies SHA256; see
CMakeLists.txt. No floating branch/tag is used. The SDK's pregenerated files
are used; Python is optional, code generation is disabled by default.
Only the Windows loader is built, not upstream tests, sample apps, Android
wrappers, Vulkan libraries or API layers. There are no game SDK dependencies.

Audit: root licenses, loader/common source SPDX notices, external JsonCpp
license, CMake linkage and generated headers reviewed. License texts are
included under docs/licenses and in the binary package. The upstream COPYING
inventory is also retained. This is a source/license scope review, not a
security certification. No sample source was copied into the host.

Windows D3D12, DXGI, D3DCompiler and MSVC runtime are Microsoft system/toolchain
components, not project-owned code. Release packaging includes the toolchain's
redistributable runtime DLLs under its redistribution terms.

## Optional camera research

experiments/camera-profile-audit/camera_fields.py adapts selected direct-layout
validation rules from ZappendusterFX/CrimsonDesertTelemetry, revision
434b3b96646b410ab8396483e31c538e051f3a4e, specifically SceneConstantsDecoder.cs
and EngineCameraReader.cs. MIT, Copyright (c) 2026 CrimsonDesertTelemetry
contributors. The full notice is preserved in
[CrimsonDesertTelemetry-MIT.txt](docs/licenses/CrimsonDesertTelemetry-MIT.txt).
The optional native-batch/selected_read.h repeats those selected range
hypotheses, with the same attribution; its acquisition code is original.
These are optional research experiments, not dependencies of the native
host or production adapter. No upstream ASI, host, dependencies or game assets
are adopted. The selected source/notice hashes are inventoried in
docs/research/crimson-telemetry-camera-inventory.json. Its layout remains an
explicit hypothesis on the current game build until actual validation.

## Optional archive and shader research

The selected PAMT layouts, metadata name-chain/flag rules and PASC wrapper checks
used by the original bounded readers in experiments/shader-consumer-audit are adapted from
Ratty123/CDMW-Full, revision 0dfc3b18f8fcf6fdfe087060dd0f0f4cdb25ca6b,
archive_format.py, models.py and archive_shader_preview.py.
MIT, Copyright (c) 2026 Ratrider. Full notice: [CDMW-MIT.txt](docs/licenses/CDMW-MIT.txt).
No upstream application, full archive parser, payload decompression/decryption
code or dependency is adopted or executed. These readers are separate from
the native M0 host and do not establish active bindings or camera semantics.
The original shader_container.py uses structural facts from Microsoft's
DirectXShaderCompiler DxilContainer.h; its selected source is fingerprinted in
docs/research/shader-container-sources.json. No compiler/validator source is
copied or shipped. The installed Windows SDK DXC compiles an original owned
fixture only; no game shader is executed by these experiments.
