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
The technique XML fragment-envelope approach also follows selected static facts
from upscale_profiles.py and native material_graph.cpp at the same revision.
MIT, Copyright (c) 2026 Ratrider. Full notice: [CDMW-MIT.txt](docs/licenses/CDMW-MIT.txt).
No upstream application or full archive parser is imported or executed. The
optional technique_contract.py executes five reviewed pure filename-format
routines selected from the SHA-pinned archive_format.py. Existing optional local
cryptography 50.0.2 (Apache-2.0 OR BSD-3-Clause) and lz4 4.4.5 (BSD-3-Clause)
provide the codec operations for bounded declaration inspection and owned tests.
No codec binary or game payload is redistributed, and these packages are not M0
or CI dependencies. This review covers their installed distribution notices,
not a complete transitive dependency audit. These experiments are separate from
the native M0 host and do not establish active bindings or camera semantics.
The original shader_container.py uses structural facts from Microsoft's
DirectXShaderCompiler DxilContainer.h; its selected source is fingerprinted in
docs/research/shader-container-sources.json. No compiler/validator source is
copied or shipped. The installed Windows SDK DXC compiles an original owned
fixture only; no game shader is executed by these experiments.

## Source-only diagnostic-tool review

The optional RenoDX inspection review fingerprints public source at revision
bc0ead39917634e95c534f7e8cfb59ab82880747. Its root MIT notice is retained in
docs/licenses/RenoDX-MIT.txt; selected devkit headers also state MIT and 2026
Carlos Lopez notices. No RenoDX/ReShade addon, bridge, replacement shader or
dependency is adopted, built or executed by this source review. The inventory
is docs/research/renodx-inspection-source-inventory.json; selected dependency
Git links are recorded as not yet license-audited, not as approved M0 deps.

## Optional owned external-tool qualification

experiments/renodx-owned builds the selected upstream MIT descriptor-cache
regression from an external, hash-locked reference tree for owned-process tests.
No upstream source is vendored and no resulting binary is installed into M0.
Selected RenoDX helpers retain their source notices; ReShade API headers state
BSD-3-Clause OR MIT, while the local ReShade runtime has the BSD-3-Clause root
notice. Selected GTL headers use Apache-2.0 and retain Gregory Popovitch/Abseil
notices. Full root texts are in docs/licenses/ReShade-BSD-3-Clause.txt and
GTL-Apache-2.0.txt, alongside RenoDX-MIT.txt. Exact paths/revisions and source
hashes are in experiments/renodx-owned/input-lock.json. This selected direct-input
audit does not claim exhaustive review of every dependency in the tool binary.
