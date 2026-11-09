# Private vertex-position influence probe

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
