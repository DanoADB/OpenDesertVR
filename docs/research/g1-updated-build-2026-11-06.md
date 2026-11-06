# G1 updated build and texture-memory experiment

An ordinary Steam launch on 2026-11-06 updated the installed game after an
initial executable preflight. The running title menu reports 2.04.08; the
installed Steam build is 26480954, executable version 1.0.0.3576, with a new
hash and valid Windows signature. The [new baseline](../evidence/g1-baseline-2026-11-06.json)
keeps these identifiers separate and retains the previous day's evidence.
No prior game trace or capture result is transferred to this build.

The normal session contains no RenderDoc layer. Its Settings mouse controls
responded. Original video settings were observed directly: 1600x1200 borderless,
DLSS 4.5 Performance, NVIDIA DLSS frame generation with the displayed count 2,
Reflex on, ray reconstruction off. Graphics used Custom, Ultra textures and
ray tracing off. These values cannot be assumed for the previous day's run.

## Diagnostic build identity

The probe now accepts only the executable hash/size/version and Steam build
combinations in experiments/frame-capture/reviewed-builds.json. The helper
independently checks app 3321460, the ready installation state, the selected
installation path and the exact hash/build combination before launching or
connecting, and checks identity again before accepting a captured frame.
A Steam content update with unchanged executable bytes is therefore rejected.
Seven tests use owned dummy bytes only; no game or capture tool is needed.
An embedded RenderDoc-Python preflight also validated this actual installation.

The new entry reviews only reuse of the same isolated early-start diagnostic
method on the observed signed Steam build. It does not authorize a new hook,
camera modification, protection workaround or claim VR compatibility. The
production adapter stays inactive. A mismatch or protection/loading failure
still stops the experiment; no arbitrary fingerprint override is added.

## Single texture-quality experiment

The original Texture Quality value Ultra was recorded before changing it to
Low through ordinary Settings and its Apply confirmation. Other settings were
left unchanged. The intended experiment is one title-menu capture from a fresh
process using the reviewed isolated Steam context. Record the actual outcome,
then restore Ultra and verify restoration in the normal game UI. The setting
change is a memory-reduction hypothesis, not a measured memory saving or a fix.

Because this build and the observed display configuration differ from the
previous day's failed capture, this is a new-build experiment, not an isolated
causal comparison against that historical failure. No raw game capture, user
config, memory contents or proprietary assets are committed. Independently
rendered world views and camera control remain unverified regardless of the
capture outcome.
