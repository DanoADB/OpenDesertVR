# ADR 0012: Explicit depth reconstruction before game capture is available

Status: accepted, 2026-11-11.

The user requested continued autonomous development without opening the game,
with an app ready for the next headset test. The stream receiver alone requires
an external eye-image producer and defaults to the earlier cube calibration.

Add a GPU linear-depth reconstruction primitive and an original procedural
color/depth source. Make their complete stream preview the desktop default,
retain tracked calibration as a separate mode, and label the transport technique.
All work runs without game data or interaction. Do not infer game depth encoding,
world scale, projection, frame association or resource state from this fixture.

The initial method uses depth-tested per-pixel splats with black disocclusions.
This provides an inspectable reference and leaves hidden surfaces visibly absent.
It does not meet the native game two-eye gate or implement head tracking. A game
capture adapter and headset acceptance remain outstanding. See the
[input contract and verification](../research/depth-stereo-preview.md).
