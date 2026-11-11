# Owned ReShade descriptor-cache qualification

This optional experiment qualifies a small loading/allocation step toward the
[RenoDX inspection lead](g1-renodx-inspection-review.md). It is not a full devkit,
draw-binding or camera experiment. Its app and addon are the original upstream
owned regression at bc0ead39917634e95c534f7e8cfb59ab82880747, not game code.

## Inputs and build

experiments/renodx-owned/input-lock.json fingerprints 25 selected source/header
and license files. It includes the upstream app/addon, RenoDX MIT helpers,
ReShade API headers (BSD-3-Clause OR MIT) and selected GTL headers (Apache-2.0,
including retained Abseil notices). The root notices are kept under docs/licenses.
This is a selected direct-input review, not a full ReShade binary dependency
audit, security certification or general approval of the full addon/toolchain.

The official ReShade 6.7.3 addon setup is downloaded as data and its embedded DLL
read from the ZIP archive; the installer is never executed. The upstream
[pinned test recipe](https://github.com/clshortfuse/renodx/blob/bc0ead39917634e95c534f7e8cfb59ab82880747/test/download_reshade.cmake)
specifies setup SHA-256 c78db69bd127e98054bd496fb422655f4a1cc664e28f8d12ce9835b2647bc571
and DLL SHA-256 ec9245d05c11751f2ac0d2256e6921ad8fb36be9172ef6d587856591eb729a25.
Both downloaded hashes match. The DLL is not Authenticode signed; its identity
here rests on those explicit content pins. It is not redistributed in M0.
The 6.7.3 source tag resolves to 4a50d1eddace85734871d91792ff214f13f66c01,
which also matches the API-header Git link in the pinned RenoDX tree.

Prepare only the locked source/header files in ignored
out/research/renodx-bc0ead3 and the DLL under out/tools/reshade-6.7.3. The separate
CMake project builds only the external owned test app and addon; no setup
script, full devkit, game shader, bridge or game addon is built. It has no install
target and no M0 runtime linkage. The selected upstream code emits MSVC warnings
for Clang pragmas and a character conversion; do not describe it as warning-free.

```powershell
cmake -S experiments/renodx-owned -B out/build/renodx-owned-controlled -A x64
cmake --build out/build/renodx-owned-controlled --config Release
```

Before executing, supply the exact built app/addon SHA-256 values to
qualify_descriptor.py using --app-sha256 and --addon-sha256. Paths and filenames
are fixed to the owned build; the controller is not a generic target launcher.
Its --configuration accepts Debug or Release; report only configurations run.

## Execution and result contract

The original controller requires clean committed source, verifies all locked
inputs and explicitly expected binaries, and records its own, lock and build
recipe fingerprints. It creates a fresh UUID case under out/g1, copies the
owned app/addon and pinned DLL there, and supplies an isolated ReShade.ini.
Supported BasePath, AddonPath, effect/texture search and cache settings point to
the case and empty directories. No user/global configuration is edited. It does
not execute the upstream launcher that recursively deletes a temporary folder.

Only the explicitly created owned child may be terminated, after a 45-second
timeout or interruption. Cleanup has its own bounded wait and a retained failure
marker. Reports/logs remain local. Hashes check staging and postflight inputs;
they do not attest every loaded code byte or prove the absence of all external
file I/O. No global hook, MCP registration or game-folder installation is made.

Require exit zero, loader/addon log observations and exactly the expected typed
result: 64 layouts, 16 threads per layout, 1,024 successful allocation requests,
zero failed requests and zero divergent layouts. Duplicate, malformed, unknown,
partial or mismatched result records reject. Three Python tests cover that
result contract plus changed/out-of-root input rejection. CI runs only these
synthetic text/file tests, not the external runtime experiment.

The external owned app creates a hidden 64-by-64 swapchain, root signatures and
one Present invocation; it does not issue calibrated geometry draws or validate
pixels. Passing this regression demonstrates the selected loader/API and
concurrent descriptor allocation behavior only. Full devkit stage coverage,
submitted draw association, per-draw GPU bytes, persistent-map behavior and
camera semantics remain unverified. This does not qualify a current-game
installation or alter ADR 0003. Headset testing remains deferred.

## Observed Release result, 2026-11-06

The [committed-controller observation](../evidence/g1-owned-reshade-descriptor-2026-11-06.json)
uses clean source ca07e21 and completes input postflight. The owned app exits
zero; log markers establish ReShade and the test addon loaded. All 1,024
requests succeed across 64 layouts and 16 threads each, with zero failures and
zero divergent layouts. The observed controller/lock/recipe and binary hashes
are retained. This is a local Release software result; no Debug runtime result
or CI execution of this external tool is claimed. Earlier development-controller
output remains in its separate local case and does not replace this record.

The first remaining qualification is actual per-draw metadata behavior on owned
vertex-only/table/rebinding/submission controls. The allocator result supplies
no pass for those tests or for current Crimson Desert compatibility.

Hosted [Debug/Release validation](https://github.com/DanoADB/OpenDesertVR/actions/runs/42267944698)
passes at ca07e21: 34 core native checks, 89 Python tests and four existing
optional research suites per configuration. The three synthetic controller
result/input tests actually ran in both. The external ReShade regression was
not run in CI; its local Release evidence remains a separate grade.
