# Owned Devkit transport and snapshot lifecycle qualification

This is an intermediate step between the [cache regression](g1-owned-reshade-descriptor-method.md)
and actual per-draw qualification. It uses the original clear-only presenting
fixture with --hidden, not a game or headset. It has no geometry draws and
cannot qualify vertex/pixel binding tracking, GPU-paired constants or camera
semantics. Those tests remain separate and required.

## Private tool build

The source is RenoDX bc0ead39917634e95c534f7e8cfb59ab82880747. The 131-file
devkit-input-lock.json records fetched source/header/license references, with
no upstream implementation vendored into M0. The private local CMake build uses
the unmodified generate_resource_rc and build_shader_target functions extracted
from the pinned root recipe. It builds the two upstream proxy shaders with the
installed SDK FXC, the devkit module and stdio bridge, and required static
Detours/MinHook libraries. No global setup script, game addon, game replacement
shader or optional Slang installation is involved. The extracted/private recipes
and build log remain local with hashes. This is a custom local build, not an
official binary or a reproducible-build attestation.

Selected direct inputs use RenoDX/Detours/ImGui/nlohmann MIT notices,
GTL/frozen Apache-2.0 notices, ReShade's SDK BSD-3-Clause OR MIT notices and
MinHook's retained BSD-family/HDE notices. Full root texts are retained under
docs/licenses; source file notices remain in the external reference. The
ReShade font-icon mapping header retains its upstream provenance comments.
No binary, font payload or tool source is redistributed in M0. This review is
not an exhaustive transitive binary audit or security certification.

The ReShade runtime is the already fingerprinted official 6.7.3 DLL. The private
devkit/bridge binary hashes and original fixture binary hash must be supplied
explicitly. The known build version is bc0ead3-owned-qualification. SDK API
headers and runtime source tag share revision 4a50d1e; that relationship alone
does not establish successful execution.

## Controller and protocol

qualify_transport.py requires a clean source tree and verifies the locked
references before execution. Fixed owned build paths are staged into a fresh
workspace case. ReShade configuration points to that case, empty effect/texture
and boot/live/dump directories and a local cache. The original fixture still
presents and honors stop.flag and its existing 60-second deadline; --hidden
only suppresses ShowWindow. No user/global config or game folder is changed.

Before connecting, Win32 GetNamedPipeServerProcessId must identify the exact
created owned child. The bridge receives its explicit PID-derived pipe name;
the pinned bridge does not fall back to other processes after an explicit pipe
selection fails. Startup observation polls that same live child for at most
eight seconds; it never restarts a target merely because observation expires.

The original bridge client accepts UTF-8 newline JSON with a 1 MiB accepted-line
limit, a two-message queue, a 32-request budget per connection and ten-second
response waits. It rejects duplicate keys, nonfinite values, mismatched/boolean
IDs and error results. Its tool allowlist covers observer selection/snapshot
and metadata reads; shader inspection is forced to summary view. Dump, clone,
replacement and shader-text operations cannot pass the request gate. This is
not a backend capture-memory limit or an exclusive owner-security boundary for
the source's local pipe DACL.

Require one uniquely identified 320-by-180 D3D12 presenting device. Queue one
snapshot, observe inactive/unqueued completion, and retain only typed bounded
summary counts publicly. Raw metadata remains private. Close the first bridge,
connect a fresh bridge to the same live child and verify status again. Finally,
request cooperative target shutdown and require exit zero. Input/controller/
binary hashes must still match, and boot/live/dump asset directories must have
no files. Each cleanup targets only processes created by this controller and
has bounded waits; failed cleanup remains recorded rather than awarded a pass.

Five synthetic Python tests cover protocol identity/size/operation gates and
unique device/completed-snapshot output selection. CI runs those tests and
compiles the original presenting fixture; it does not fetch or run the external
tool. A later observed transport result must remain separate from actual draw
binding coverage, shader semantics, GPU completion and the G1/G2 game gates.

## Observed Release transport result, 2026-11-06

[Clean source 07d12e8 evidence](../evidence/g1-owned-devkit-transport-2026-11-06.json)
records the matching owned-child pipe, completed snapshot, successful fresh
bridge connection and cooperative target exit zero after six original fixture
frames. All retained snapshot counts are zero, consistent with clear-only work.
There are no boot/live/dump asset files, and source/binary postflight passes.

This qualifies only that private build's transport and snapshot lifecycle on
the selected owned app. Actual geometry draw tracking, vertex-only descriptors,
submission markers and per-draw constant bytes remain NOT_RUN/unverified. The
next test needs an owned geometry fixture with those explicit controls.

Hosted [Debug/Release validation](https://github.com/DanoADB/OpenDesertVR/actions/runs/42271236573)
passes at 07d12e8: 34 core native checks, 94 Python tests and four existing
optional research suites per configuration. The five new synthetic protocol/
device/snapshot tests actually ran in both. CI compiles the modified original
presenting fixture; external Devkit execution remains the separate local
Release observation, with geometry/draw-binding qualification still untested.
