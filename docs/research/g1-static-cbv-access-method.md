# Selected shader constant-buffer access observation

The live world candidate uses b18/space35 in a finite descriptor table. Root
metadata alone does not establish whether the shader selects one descriptor
or reads within the captured 2,816 bytes. This narrow observation examines
DXC text from the exact retained pipeline shader inside the observer process.
It exports numeric counts and row indices only. Shader bytes, names and
instruction text remain in process; no shader or pipeline is replaced.

`static_cbv_access.hpp` resolves the CBV list through `dx.resources`, requires
one singleton declaration covering the target register, and follows supported
SM6.0 `createHandle` or SM6.6 `createHandleFromBinding` plus annotations. It
collects all handles before inspecting uses, including forward references.
Only literal-index f32/i32 `cbufferLoadLegacy` uses are accepted; each complete
16-byte row must fit both the declared buffer and the explicit byte limit.
Dynamic loads, descriptor arrays, unsupported handle uses and heap/library
handle origins return an unsupported observation. The analysis intentionally
rejects some unrelated dynamic handle creations rather than guessing aliases.

Input text, line length, metadata count, nesting and target-handle count have
fixed bounds. DXC text is compiler output, not an arbitrary LLVM module accepted
as verified code. This helper is not a general IR verifier. Microsoft documents
the resource records, handle operations and constant-buffer loads in its
[DXIL reference](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst).

`staticLegacyLoadBoundsObserved` is a bounded syntax observation, not proof that
the draw executed, that a camera field controls world pixels, or that a frozen
payload is safe. It does not follow loaded words through arithmetic, branches,
resource indices or later shader stages. `downstreamConsumptionProven` and
`frozenPayloadSafe` remain false. Current-frame source ordering, resource
lifetime, complete PSO/root compatibility and camera influence remain separate
requirements before enabling a private game-camera probe.

The isolated reflection CMake project runs authored negative controls and
compiles original HLSL with Windows SDK DXC for SM6.0 and SM6.6. Constant last-row
reads must be observed at byte end2,816; dynamic rows must be rejected. No GPU
or game is needed for these tests. The observer's separate owned runtime control
must also verify unchanged pixels, normal exit, no leaked assets and numeric
observations from a retained shader before staging it with the game.
