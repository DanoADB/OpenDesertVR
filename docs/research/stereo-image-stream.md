# Stereo image delivery

The core can now move an independently produced left/right D3D12 image pair
between processes and submit it to OpenXR. This is the delivery half of the
Crimson Desert showcase. It does **not** capture the game or create its missing
second camera view. Crimson Desert stereo and headset acceptance of this new
path remain unverified.

## Run

A producer creates `odv::d3d::StereoPublisher` on its direct queue and supplies
two RGBA8 textures with known resource states. It passes `publisher.channel()`
to either consumer:

```text
OpenDesertVR.exe --stereo-stream CHANNEL
odv-testhost.exe --stereo-stream CHANNEL --config config/testhost.json --report stream-report.json
```

The desktop app starts receiving automatically, provides Stop and saves reports.
The CLI also accepts `--stereo-stream-file FILE`, waiting at most five seconds
for a bounded channel-name file. A missing or disconnected source produces an
error, never a fallback to the cube scene. Without stream arguments, both apps
retain the existing independent synthetic stereo test.

The desktop stream session is limited to 60 seconds. Its head-tracking checkbox
is disabled: importing static-camera images does not drive a game camera. The
current fullscreen import preserves source aspect ratio with black bars. Source
FOV, lens matching, world scale and comfort have not been calibrated for a game.

## Ownership contract

The transport uses two slots, each containing separate shared left/right textures,
and shared ready/released fences. One consumer may hold one complete pair while
the producer advances the other slot. Busy slots drop the new pair without waiting
for the consumer GPU. It never exposes a partially completed copy. Frame identifiers
must match or be adjacent; this is a bounded association check, not proof that a
game produced either eye correctly.

The producer copies on its exact direct queue, retains both inputs through copy
completion and restores the caller's declared resource states. Shared images are
COMMON between owners. The reader submits all reads on its fixed direct queue,
restores COMMON and releases the lease after those commands. It must not retain
commands using a released lease. Completed fence values establish cross-queue
ordering before reuse. Teardown waits are bounded; unknown retirement retains
GPU references until process exit rather than freeing resources still in use.

The current version is same-adapter, Windows-local, single consumer, RGBA8,
single mip, non-MSAA, with dimensions bounded to 8192 per side. The source declares
linear or sRGB color. Channels use local named kernel objects and default process
security; there is no network service. Numeric reports omit names, handles and PIDs.

Microsoft documents the underlying [shared-handle API](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle),
[shared heaps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps)
and [queue/fence synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization).

## Evidence and remaining work

`odv_stereo_stream_warp` checks exact independent-eye pixels across devices and a
child process, texture rendering, held-pair retention, reconnect, source restoration,
incomplete-copy rejection and nonblocking backpressure. `odv_xr_stereo_stream`
streams moving owned stereo into the mock runtime, checks balanced swapchain
ownership and tests producer disconnect and missing-source failure.
`odv_stereo_stream_validation` runs the transport/texture checks with the D3D12
debug layer when installed; exit 77 explicitly records its absence.

These are owned-content tests. They do not prove game-camera control, whole-world
eye coverage, native game stereo, live runtime FOV matching or headset comfort.
The next integration step is a game producer whose final eye images have a proven
relationship to two world-camera renders; observer35's selected-terrain scalar
probe is still awaiting a visible game scene. Origin `GAME_CANDIDATE` is a label,
not validation. Reports keep `game_stereo=UNVERIFIED`,
`game_camera_tracking=NOT_RUN` and hardware/game validation separate.
