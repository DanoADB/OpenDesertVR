# Camera input and reversible consumer experiment

The [controlled left turn](../evidence/g1-game-native-enhanced-2026-11-09.json)
correlates one active DEFAULT input with the user camera. Two completed GPU
samples pass selected position/basis/view/projection consistency checks. The
same source binding changes 27.865 degrees, horizontally 27.862 degrees left,
while resolution and projection remain unchanged. This is input correlation,
not demonstrated camera control or stereo.

Read-only shader summaries identify the sampled draw as a dynamic-shadow
vertex/pixel pair. Both stages bind the candidate. The square 1024x1024 render
targets are separate from the 5120x1440 presentation surface. First locate its
visible-world consumer; changing a shadow-only input would not establish a
world-view change. A bounded capture of screen-shaped targets is the next
specific question, keeping the 128-record/six-detail limits.

Observer12 implements that read-only heuristic behind ODVSceneAspectFilter=1.
It accepts a substantial color target within 2% of the primary swapchain aspect,
between one quarter and twice its dimensions. Mip dimensions are accounted for;
absent presentation/target metadata rejects the pass. Screen-shaped UI and
postprocessing can also match, so retained shader/binding metadata still needs
review. A square primary display would not exclude square shadow targets.

The [owned paired control](../evidence/g1-owned-native-scene-2026-11-09.json)
executes a square pass before the usual screen-aspect draws. Filter-on excludes
it; filter-off retains it. Both preserve authored GPU bytes, unsubmitted
rejection, independent rendered pixels, normal exit and input postflight. The
initial fixture accidentally rebound the main target inside its draw helper;
the failed reports are retained, and the corrected fixture qualifies both
settings. This does not yet qualify the heuristic on the game.

## Owned GPU perturbation

`ScopedScalarPatch` can copy a bounded constant range into a private DEFAULT
buffer, change one float in that copy, and provide its private GPU address for
scoped binding. Original input bytes are never written on this path. GPU copies
save/restore the private value. Allocation precedes recording, supported
enhanced barriers establish access scopes, and covering fences plus command
recording retirement govern release. Six retained patches are the hard bound;
unknown completion/retirement quarantines resources and the capacity slot.

Local Debug and Release each pass seven optional native tests. The private-copy
test independently reads all 2816 original bytes while the private copy is
changed and again afterwards. The authored rendering fixture binds the private
copy, shifts its triangle out of view, verifies 2048 black pixels, then restores
the original binding and verifies all 2048 RGB/black pixels. All 256 original
input bytes remain unchanged. The private-copy byte test also passes on the
physical GPU. Local debug layer is unavailable; hosted validation is separate.

Hosted [Debug/Release CI for 299f8db](https://github.com/DanoADB/OpenDesertVR/actions/runs/42654704955)
passes. Downloaded logs explicitly mark all four enhanced-barrier tests NOT_RUN
because that runner's WARP lacks support; this is not enhanced debug-layer
acceptance. The legacy DEFAULT test runs with debug=ENABLED in both jobs. The
first hosted scalar-render run failed on the older feature-query path; the fix
recognizes an unavailable OPTIONS12 query and reports a skip, while unexpected
query failures remain failures. Local enhanced/geometry results remain a
separate evidence grade from hosted CI.

The in-place entry point is an owned, exclusive-source control only. A barrier
on one queue cannot order another queue's reads; the game's shared camera
buffer has no established exclusive-queue contract. Do not enable in-place
patching on it. Game integration must replace a draw-local binding with a
private copy and restore every affected binding, preserving the actual native
draw and indirect signature. The owned root-CBV fixture does not qualify the
game's descriptor-table replacement route. Do not change shared descriptor
heaps used by other recordings or read shader-visible heaps as descriptor-copy
sources. Root-signature/PSO remapping is a candidate route, not implemented proof.

No game projection field has been changed; independent game-eye rendering and
headset submission remain NOT_RUN. Source data and shader bytecode stay private.

Primary reference: [Microsoft descriptor-copy source restrictions](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-copydescriptorssimple).

## Same-stage geometry-input read

The screen-aspect snapshot finds a standard geometry vertex/pixel pair writing
five 2560x720 targets. Both stages bind b18 space35, a 2816-byte range shared
with the reviewed particle/refraction inputs in that same snapshot. Numeric
confirmation in this launch remains absent: all eleven queried copy sources
were DEFAULT_STATE_UNVERIFIED. This is a geometry-input candidate, not world
camera control. The process was later absent; exact staged-file cleanup and
unchanged game identity passed. Its exit cause was not observed.

An [owned vertex-stream control](../evidence/g1-owned-vertex-stream-2026-11-09.json)
reads a CBV using a vertex shader into uint4 stream output with rasterization
disabled. It does not add COPY_SOURCE access to the original source. All 2816
bytes match on WARP and physical GPU, including integer and NaN bit patterns;
independent verification finds all 4096 original source bytes unchanged.
Unpredicated stream-output statistics reject a GPU-skipped draw/copy. Local
Debug/Release each pass eight optional tests; local debug layer is unavailable.

This standalone executable does not yet restore an application's state. Before
game use, qualify full affected-state replay, active-query and render-pass
rejection, active vertex-CBV evidence, bounded resource retention and exact
submission/retirement tracking. Pixel-only bindings cannot qualify a vertex
read. This route reads inputs; it does not establish a private-binding write or
separate eye rendering.

References: [Microsoft stream-output description](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_stream_output_desc)
and [predicated versus unpredicated operations](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication).
