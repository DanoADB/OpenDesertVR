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
