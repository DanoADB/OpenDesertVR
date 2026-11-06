# Offline camera-profile audit

Original, optional research code; no production build/install dependency.
This checks published instruction guards against a fingerprinted executable
file without loading it or accessing any running process. It does not modify
the game, resolve a live camera, execute an upstream mod or relocate a profile.

The reviewed lead is ZappendusterFX/CrimsonDesertTelemetry at
434b3b96646b410ab8396483e31c538e051f3a4e, with an MIT root license. Its
definitions/build-26448710.json identifies our historical executable exactly.
That does not establish support for the updated installed build. Keep the
external source checkout in ignored out/research; no upstream source, offset
table or dependency is adopted into this experiment.

audit_profile.py requires the expected executable and profile SHA-256 values,
a fresh output file, a bounded x64 PE image and valid instruction-pattern
syntax. It counts matches only within executable sections, checks whether the
published reference RVA matches, and counts the published vtable-function
fingerprints. It decodes only the pinned guard's RIP-relative MOV reference
and checks that the target is aligned writable data. For unique function
matches it counts read-only tables containing the preferred-image function
addresses in the specified slots. This does not establish a C++ type or a live
relationship between the global, camera object and pointer table. Reports omit
instruction bytes, locations, machine paths and proprietary payloads.

Run with Python using --executable, --profile, --expected-executable-sha256,
--expected-profile-sha256 and --output. Pin and review the external source
before supplying its profile. Rehash the executable before and after observation;
any mismatch stops the run. Output creation is exclusive. All fixtures in
tests/test_camera_profile_audit.py contain owned synthetic PE bytes only.

One or even several unique matches are static leads only. No result awards
runtime compatibility, camera control or stereo; those remain UNVERIFIED.
Live process access or camera/render intervention needs a separate scoped review.

PE handling follows Microsoft's documented file/RVA/preferred-base distinction.
Raw and virtual section overlaps are rejected. Zero-filled writable data can
satisfy a global's section classification but cannot be read as file-backed
bytes. Pointer tables must remain entirely in one read-only file-backed section;
duplicate candidates remain ambiguous. Function guards must be independent.
