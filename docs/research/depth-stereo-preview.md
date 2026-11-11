# Offline depth stereo preview

The desktop app now defaults to **Offline depth stereo - owned courtyard**.
Start runs a static original scene through matched color/depth, GPU left/right
reconstruction, the image-pair transport, and OpenXR for up to 60 seconds. Stop
requests orderly shutdown. The original tracked cubes remain in the mode menu.
No scripts, game launch or game assets are needed. Changing modes clears the
observation checklist and requires a new test before observations can be saved.

The scene contains stone-colored pillars, a floor, a near amber sphere and a
farther teal sphere. Depth comes from the same procedural ray hit as each color
pixel, with positive axial Z in metres. The source is 640x360 with a known focal
length and 64 mm total eye separation. The camera is static. This is an owned
reconstruction test, **not Crimson Desert capture, native game stereo or game
head tracking**. This build's headset acceptance remains NOT_RUN.

CLI equivalent:

```text
odv-testhost.exe --depth-preview --config config/testhost.json --report preview-report.json
```

## GPU reconstruction contract

`Renderer::render_depth_eye` takes a same-device RGBA8 color image and R32_FLOAT
linear axial depth image of equal dimensions, plus horizontal focal length in
pixels, eye offset in metres, near/far bounds and three matching nonzero frame
identifiers (color, depth, projection). Inputs arrive and leave in COMMON on the
renderer queue. The RGBA8 target starts and remains RENDER_TARGET. Sources and
target must be distinct, single-mip, non-MSAA textures, at most 2048x2048.

The GPU projects each source pixel's one-pixel quad horizontally by
`-focal_x_pixels * eye_offset_m / axial_depth_m`. A private D32 depth buffer
resolves collisions in favor of the nearer source sample. Zero/negative depth,
NaN, infinity, and samples outside the near/far interval are discarded. Uncovered
pixels remain black. It does not interpolate across silhouettes or invent hidden
surfaces. Curved surfaces may also show small gaps with this initial pixel-splat
method. Transparency, HUD separation, lens/FOV calibration and temporal hole
handling are not implemented.

Raw hardware depth, reversed Z, nonlinear device depth, unknown scale or unknown
projection are not valid inputs. Matching identifiers are a caller contract, not
proof that two arbitrary game textures belong together. A future game producer
must establish actual same-frame color/depth/projection association, encoding and
resource state on the submitting queue before calling this function.

The transport protocol is now version 2 and carries an explicit technique:
`DEPTH_RECONSTRUCTED` or `PRODUCER_SUPPLIED_EYES`. It rejects older/incompatible
headers. Origin `OWNED_GEOMETRY`/`GAME_CANDIDATE` remains separate from technique
and neither field awards game validation. Preview reports include
`owned_depth_preview=true`, `game_stereo=UNVERIFIED`, and
`game_camera_tracking=NOT_RUN`. GPU timing for the stream is still NOT_RUN.

## Verification and next integration step

WARP tests compare identity pixels and both eye shifts with an analytic integer
reference, including nearest-depth overlap and black uncovered regions. They
check invalid depth, mismatched frame/projection IDs, unchanged source bytes and
COMMON restoration, and actual reconstructed eye textures entering the transport.
Mock OpenXR tests cover advancing publications, tracking gaps, session loss,
missing headset and cooperative Stop. Existing cross-process stream tests remain.
D3D12 debug-layer tests skip explicitly when Graphics Tools is absent.

Desktop visual inspection confirmed the default mode, selectable original mode,
appropriate head-tracking checkbox state and missing-headset readiness message.
No headset or game acceptance was performed. The next game task is acquiring a
proven matching color/depth/projection frame; this change does not guess one or
alter the existing live observer. Independent native game eye rendering remains
a separate milestone.
