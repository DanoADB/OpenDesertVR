# Private vertex-position influence probe

Probe18 runtime configuration requires `ODVClipProbeEnabled=1`,
`ODVDefaultConstants=1` and `ODVNativeMetadata=1`. The latter two install the
native command-list state and draw/indirect callbacks needed by the probe.
`ODVUploadInputs` remains0. No snapshot or constant-buffer copy is requested
by the clip probe. These settings are read at addon initialization, so changing
them requires a normal game restart. A mode1 status with zero recorded
replacements is not an executed identity control. The first game attempt
exposed this configuration error; see the
[attempt record](../evidence/g1-game-clip-probe18-2026-11-10.json).

The observed world VS has bounded target-CBV loads, while three PS variants
still contain unsupported target-handle uses. A private vertex shader can test
visible geometry influence without copying or changing any game buffer and
without lifting a descriptor-table CBV into the root signature.

The owned implementation identifies one VS entry point, its exact float4
SV_Position output element and one X-component store. It inserts one fixed
`clip.x += delta` instruction, using delta0 for the identity control and
delta+/-0.125 for the two geometry controls. All other instructions and shader
metadata remain unchanged. Unsupported/ambiguous output layouts and non-VS
input are rejected. DXC text and assembled shaders stay in process.

Microsoft's SDK [assembler](https://learn.microsoft.com/en-us/windows/win32/api/dxcapi/nf-dxcapi-idxcassembler-assembletocontainer)
accepts LLVM-style DXIL. The modified container must also pass its
[validator](https://learn.microsoft.com/en-us/windows/win32/api/dxcapi/nf-dxcapi-idxcvalidator-validate)
before PSO creation. Assembly and validation do not by themselves prove useful
rendering; the owned fixture checks GPU pixels and preserved inputs.

The original owned quad is rendered at clip-W1 and2, using the same original
root signature, exact40-byte IBV/root0/DrawIndexed command signature, original
CBV and argument/count/index buffers. Only the PSO changes. Eleven cases cover
original and identity output, both shift directions, near/far displacement,
original PSO restoration and GPU count0/max0. Every case checks1,024 pixels.
The shift creates4-pixel blank strips at W1 and2-pixel strips at W2. Camera,
DEFAULT argument/count and index bytes must remain exact after a covering GPU
fence. The harness runs both SDK-compiled SM6.0 and SM6.6 shaders and rejects a
pixel shader supplied as the VS. No game data is used by these controls.

This is a geometry-influence diagnostic, not a finished alternate camera or
stereo solution. A constant homogeneous-X change produces depth-dependent
displacement; a constant multiplied by W would instead make a uniform screen
pan. Neither technique alone fixes original-camera culling, disocclusions,
temporal effects, lighting or headset pose/render delivery.

Game integration remains NOT_RUN. It requires preserving the actual native PSO
description, including fields omitted by generic ReShade pipeline subobjects
(for example sample quality, node mask, strip-cut and view-instancing state).
The private PSO must retain the original root and unchanged PS/other stages,
forward the original native indirect call once, restore the original PSO and
remain alive through submitted GPU use. No shader/root/signature replacement
is enabled in the current game observer.

## Active query correction (Probe19)

The corrected Probe18 configuration found 6,123 tagged world-shader candidates,
but recorded no replacement draws. A bounded current-scene snapshot found an
active GPU query on both matching world draws. The generic replay eligibility
gate therefore rejected them before variant creation.

Probe19 uses a separate single-forward eligibility gate: known active queries
may surround the unchanged application command, forwarded exactly once. Unknown
state, missing native capabilities, protected sessions, render passes and stream
output remain rejected. Replay/copy eligibility is unchanged. Owned GPU controls
now wrap all eleven cases in pipeline-statistics and occlusion intervals; exact
primitive and covered-sample counts detect accidental double execution.
Debug and Release SM6.0/6.6 legacy/stream controls pass locally. Local D3D12 debug
validation remains unavailable; hosted validation is separate. Native wrapper
attachment controls also pass with the new addon and tracking enabled.

The game retry with Probe19 remains NOT_RUN until the current game exits normally
and the qualified observer is staged. No game stereo or headset result follows
from these owned tests.

Probe19 live retry reached shader processing but rejected its first identity
candidate. No replacement draw was recorded; mode0 was restored and the original
scene continued. Probe20 adds fixed numeric compiler-stage/HRESULT reporting
without compiler text or shader export. Owned non-vertex controls verify stage4
rejection, and Debug/Release geometry controls pass. Probe20 game retry is NOT_RUN.

Probe20 live retry isolates stage6 validation failure, HRESULT0x80AA0009
(DXC_E_IR_VERIFICATION_FAILED, per Microsoft ErrorCodes.h). No replacement draw
was recorded. Probe21 checks the untouched original container and an untouched
assembly round trip before the clip rewrite. These controls distinguish compiler
compatibility from an edit-induced error; neither bypasses validation. It exports
only a bounded plain-language diagnostic line, rejecting instruction, function
and quoted-identifier excerpts. Owned Debug/Release tests and native attachment
controls pass. Probe21 game retry is NOT_RUN.

Probe21 live control rejects the untouched original shader at stage7 with the
same IR-verification HRESULT; the round trip and clip rewrite are not reached.
The next isolated test changes only the private compiler/validator sidecars to
Microsoft.Direct3D.DXC1.9.2609.5, pinned from Microsoft NuGet with valid Microsoft
Authenticode signatures and retained licenses. Owned native legacy/stream
controls pass with this pair. Compatibility with the game original remains
UNVERIFIED until the new live control. No validation is disabled.
