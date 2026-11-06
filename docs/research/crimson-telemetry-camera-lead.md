# Native camera telemetry: new pinned source lead

Reviewed 2026-11-06 at ZappendusterFX/CrimsonDesertTelemetry revision
434b3b96646b410ab8396483e31c538e051f3a4e. The root license is MIT. Selected
files and hashes are recorded in crimson-telemetry-camera-inventory.json.
No upstream source was built, executed, installed or adopted. Its root license
does not complete the dependency audit for a future integration.

## What changed the investigation

The project's [build-26448710 profile](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/blob/434b3b96646b410ab8396483e31c538e051f3a4e/definitions/build-26448710.json)
exactly identifies our historical November 5 executable, including SHA-256 and
version 1.0.0.3549. It explicitly disables automatic compatibility for that
profile. Our installed November 6 executable is a different fingerprint and
Steam build 26480954. Upstream's locally-validated label is its own result,
not our acceptance evidence.

[EngineCameraReader](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/blob/434b3b96646b410ab8396483e31c538e051f3a4e/src/CrimsonDesertTelemetry.Core/EngineCameraReader.cs)
implements guarded observation of a native camera source, not just an FOV scalar
or historical Streamline-copy consensus. Its direct layout checks a camera
object type and source pointer, compares successive reads, and uses the decoded
buffer frame number to reject a source that stops advancing. Retries are bounded.
Those measures improve source/freshness checks but do not guarantee an atomic
engine-frame snapshot or establish a camera write/render-scheduling interface.

The [historical engine research](https://github.com/ZappendusterFX/CrimsonDesertTelemetry/blob/434b3b96646b410ab8396483e31c538e051f3a4e/docs/ENGINE_CAMERA_RESEARCH.md)
records camera-only motion and upscaling-off observations on an older build.
These author reports explain the lead's provenance; we have not reproduced
them on either of our fingerprints. The broader native ASI also contains hooks
and graphics capture for unrelated lighting features. Do not install that whole
package merely to inspect the camera or treat all its features as necessary.

## Actual current-file observation

The original optional tool in experiments/camera-profile-audit reads only the
fingerprinted executable file and pinned external profile. It neither loads the
game nor accesses a process. It checks PE bounds and executable sections, counts
instruction-guard matches, and exports no locations or proprietary bytes.

The [actual observation](../evidence/g1-camera-guard-observation-2026-11-06.json)
found one occurrence of the published camera-reference guard and one occurrence
of each of two published camera-vtable function fingerprints in the current
executable. The reference does not occur at its published old RVA. Both file
hashes were required, and the executable was rehashed after observation.
Owned synthetic PE tests cover non-executable lookalikes, section-boundary
matches, ambiguity, malformed input and wildcard matching. All 28 Python tests
passed locally after this addition; CI executes only owned bytes.

This is a concrete current-build static lead, not a relocated profile. The
tool does not prove those functions share one vtable, that a derived global
points to an active camera, or that the expected live layout remains valid.
It does not grant runtime compatibility or change the production adapter.

## Next narrowly scoped proof

First establish the reference target and multi-slot vtable relationship in a
separate offline observation, retaining ambiguity and failure results. If that
succeeds, review a minimal bounded read-only camera observation against the
exact build, with source replacement/frame-progress and controlled camera-only
movement checks. Do not carry forward old object-field offsets as proven.
Any live process access requires its own scoped technical review first.

Camera observation alone still leaves projection writes, independent world-view
scheduling, temporal-history ownership and XR submission unverified. No G2 or
playable game VR capability is awarded. The user has deferred further headset
tests; this file-only research does not need a headset or substitute for its
unperformed checks.
