# First bounded game metadata observation

Purpose: establish actual current-game pipeline/CBV metadata for camera research.
This is not a stereo renderer, a camera offset discovery by assumption, or G2.
User priority is a Crimson Desert VR showcase; further cube-app polish is held.

## Qualified inputs

- Reviewed Steam build 26480954, executable SHA256
  `c94ad6c3ea54970a6d2da7a2719753210a7728a91698ed3405dd9125470ed98f`.
  Re-run `validate_game_build` from the existing diagnostic build gate.
- Pinned ReShade 6.7.3 full-addon DLL and isolated private RenoDX
  `bc0ead3-bounded-observer-2`, input lock
  `experiments/renodx-owned/devkit-bounded-input-lock.json`.
- Original bridge and SDK DXCompiler fingerprints retained in the owned cases.
  [Actual qualification](../evidence/g1-owned-bounded-observer-2026-11-06.json)
  verifies vertex/pixel identities, known versus unsubmitted command lists,
  128-record cap/truncation under 150 submitted draws, unchanged independent
  GPU pixels, and bootstrap configuration redirection.

## Loading and isolation

Observe the game at its title menu before requesting normal exit. Do not end
an unsaved gameplay session. Verify exit before staging. Preserve every
existing game-directory file; reject staging if `dxgi.dll` or `ReShade.ini`
already exists. Create only those two new files, exclusively, beside the
reviewed executable. `dxgi.dll` is the fingerprinted official ReShade DLL.
The bootstrap INI contains only `[INSTALL] BasePath=<fresh workspace case>`.

The separate case holds the full INI, single observer addon, bridge, SDK
DXCompiler, logs and reports. Its addon search path is local to that case;
its effects, textures and boot/live/dump directories are empty. Disable effect
loading/reload/cache, automatic dumps/traces and device proxy mode. Use the
case's explicit ToolsPath. No replacement shaders, game addon, buffer writes
or shader/text/dump RPC are used. No global runtime/MCP registration or game
executable change is made. Protective environment flags remain untouched.

Start the game through its normal Steam launch. Verify the new process's exact
image path, creation time and reviewed on-disk build; retain a limited-query
process handle to reject PID reuse. The explicit pipe's server PID must match
that expected live process, and its server version must match the qualified
observer. Loading failure stops this method; no protection workaround follows.

## Observation

Wait for a presenting D3D12 device, explicitly select its index and request
one snapshot. Wait for inactive/unqueued completion before reading it. The
observer retains at most 128 draw/dispatch/copy records and marks truncation.
At most four pages of 32 summaries and a small number of selected details are
queried. Keep total bridge requests within the existing 32-call client limit.
This bounds retained records and exported responses, not all internal caches
or total game/tool memory. The snapshot is a prefix and may omit world work.

Permitted RPCs: status, select device, queue snapshot, snapshot summary, list
draws and get draw. Shader text, dumps, cloning, replacement and constant-byte
readback are excluded from this first observation. Raw handles and paths stay
private. Public evidence contains counts, types, dimensions, fingerprints and
interpretation only. A title menu is not evidence of a world camera.

Use submissionKnown/order to separate known queue submission from unknown
recording. Neither proves GPU completion. CBV register/space aliasing across
stages, resource lifetime and camera semantics remain unresolved. Do not read
native pointers after the callback merely because a handle was exported.

## Exit and acceptance

Close the probe game normally. Verify process exit, build/source/tool hashes
and empty boot/live/dump directories. Remove only the two files created by
this case, after verifying their exact recorded hashes and parent directory.
The owned bootstrap redirect test confirms its INI remains unchanged even
while the full case INI changes. If either staged file differs, preserve it
and report the cleanup exception. Retain case evidence; no recursive cleanup.

Success means the reviewed build actually loaded this bounded observer and
returned correctly labeled metadata with orderly cleanup. It does not award
camera control or VR compatibility. The next step is to identify world-pass
camera candidates, acquire relevant values with a separately justified
lifetime/coherence method, and prove an actual game-view change before stereo.
