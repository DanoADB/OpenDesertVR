# Isolated same-container reflection observation

The [structural observation](g1-single-shader-container-method.md) found a STAT
bitcode header in one explicitly selected pixel shader. This method asks whether
that same container supports compiler reflection and what declaration shapes
cover nine previously published hypothetical byte positions. It does not
identify a camera shader or establish a CPU/GPU buffer mapping.

## Owned helper and output contract

The original optional C++20 helper accepts at most 1 MiB of binary DXBC on stdin.
It loads an explicit installed Windows SDK dxcompiler.dll into its own process
and calls Microsoft's [IDxcUtils::CreateReflection](https://learn.microsoft.com/en-us/windows/win32/api/dxcapi/nf-dxcapi-idxcutils-createreflection),
then ID3D12ShaderReflection. It does not compile shaders or submit work to a GPU.
There is no process attachment, game loading, hook or install target. The
library is not copied, redistributed or linked into M0.

Require at most 64 reflected buffers, 512 bound resources, 2,048 variables per
buffer, 4,096 variables overall, 64 KiB buffer spans and 512 query matches.
Query only constant-buffer declarations covering offsets 0x20, 0x30, 0x80,
0x90, 0x3e0, 0x420, 0x4e0, 0x860 and 0xac0. These positions remain historical
hypotheses. Output buffer ordinal, declaration span, exact-start boolean,
primitive-type category, class/shape/array/member counts and compiler-used flag.
The ordinal is a reflection enumeration index, not a D3D12 binding register.
The total variable count includes reflected non-constant-buffer descriptions;
query records come only from constant buffers. Struct members are not traversed.

A declaration covering an offset can be an array or composite with a different
start. Even an exact start and a matching primitive type do not identify its
game role. Compiler-used flags leave actual byte consumption, runtime binding,
frame association and producer ordering unverified. No buffer/variable names,
IR, disassembly or raw payload are exported. Native output is capped at 128 KiB.

## Parent and game-input gates

The parent requires explicit helper/SDK-library file SHA-256 values, checks
them before and after use, and requires structurally qualified DXIL and STAT
headers. The native process receives bytes on stdin and returns a fixed JSON
schema. The parent applies a 10-second subprocess timeout; cleanup may add time.
Stderr is discarded and failed/malformed/oversized responses cannot produce a
pass. Rebuild the export from validated fields so arbitrary extra text is not
retained. This is process separation, not a security sandbox or hard process
memory limit; file hash checks do not attest every loaded code byte.

The game-facing wrapper retains the existing exact installation/index/entry,
range/file and game/catalog pre/post checks. It additionally requires the
payload and container SHA-256 values from the prior structural observation,
before invoking reflection. There is no automatic alternate shader, retry,
decompression or decryption. Raw input stays in reader/helper memory; no file
containing game shader bytes is created. Reserve evidence exclusively and keep
sanitized FAILED/stage records on rejection.

## Qualification

The original owned HLSL fixture is compiled separately with Windows SDK DXC.
Native tests query both uint and float words at 0x20, a float3 at 0x80, matrices
at 0x3e0/0x4e0, and a pixel shader without a constant buffer. Exact offsets,
sizes, primitive categories and used flags are checked, and names stay out of
output. Malformed and oversized native inputs reject. Both local Debug and
Release suites pass. No fixture is run on a GPU.

Python tests cover typed schema/span validation, missing STAT, tool fingerprints,
timeout/nonzero/malformed/oversized output, postflight tool changes, expected
payload/container mismatches before helper invocation, and exclusive/sanitized
failure reports. CI uses owned data and SDK tools only, with no game or headset.

## Interpretation

OBSERVED_SHADER_REFLECTION_METADATA means this individual container was
reflected by that SDK library. Zero query matches can be a valid result and
must not trigger another shader selection. All camera layout, active binding,
publication, control and stereo capability fields remain UNVERIFIED regardless
of matching declarations. G2 still needs independently rendered world views;
synthetic M0 acceptance does not supply that missing engine interface.

## Actual same-container result

The [observation](../evidence/g1-single-shader-reflection-2026-11-06.json) ran
committed source 641385d once on reviewed build 26480954, using the exact prior
index, entry, payload and container fingerprints. The same 4,088-byte range
was read twice; the original range/file/index/game/catalog checks and helper/
SDK-library hash checks passed. Native Release helper SHA-256 and SDK-library
SHA-256 are recorded independently of reader source hashes. Exit 0.

The SDK reports zero constant buffers, one bound resource, zero reflected
variables and no query matches. This establishes that this individual container
can be reflected with this SDK, while supplying no queried camera-field
declarations. Do not generalize that table result to other shaders, unresolved
resource access paths, active game bindings or absence of a camera interface.
Raw payload, names, IR and disassembly were not saved, and no shader was
submitted to a GPU. The game process was not accessed or modified.

Stop observations on this witness here. Further selection needs concrete
current-build evidence connecting a shader to a relevant material/pass or
render-camera consumer, or another verified producer/consumer interface.
Do not switch arbitrary entries or scan until camera-looking declarations
appear. This reflection tool is research infrastructure, not a game adapter;
the publication, control, world-render and stereo gates remain unchanged.
