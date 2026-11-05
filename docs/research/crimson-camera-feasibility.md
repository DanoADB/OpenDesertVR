# Crimson Desert camera feasibility — initial source review

This review separates evidence of editable camera settings from the runtime
camera/render control required for VR. No upstream mod was built, installed,
executed or adopted, and no game archive was decoded or modified.

## Public source: UltimateCameraMod

Pinned repository: fitzforge/UltimateCameraMod,
revision d84e1a8a7812e8afc4671434962c12033eab877c. The
[root license](https://github.com/fitzforge/UltimateCameraMod/blob/d84e1a8a7812e8afc4671434962c12033eab877c/LICENSE)
is MIT; this is not a complete transitive licensing or redistribution clearance.
Seven inspected files are fingerprinted in the
[source inventory](crimson-camera-source-inventory.json).

The selected source supports these findings:

- [CameraMod.ApplyModifications](https://github.com/fitzforge/UltimateCameraMod/blob/d84e1a8a7812e8afc4671434962c12033eab877c/src/UltimateCameraMod.V3/Services/CameraMod.cs#L57)
  consumes XML text and a modification set. Its install path at
  [lines 1133–1160](https://github.com/fitzforge/UltimateCameraMod/blob/d84e1a8a7812e8afc4671434962c12033eab877c/src/UltimateCameraMod.V3/Services/CameraMod.cs#L1133)
  reads a backed-up camera configuration, changes values and writes an archive
  entry. This is a file-editing path, not a demonstrated runtime pose API.
- [CameraPreview](https://github.com/fitzforge/UltimateCameraMod/blob/d84e1a8a7812e8afc4671434962c12033eab877c/src/UltimateCameraMod.V3/Controls/CameraPreview.cs#L8)
  is a WPF Canvas. Its parameter update and drawing methods visualize a camera
  diagram. That UI preview is not evidence of additional game-world renders.
- The [project file](https://github.com/fitzforge/UltimateCameraMod/blob/d84e1a8a7812e8afc4671434962c12033eab877c/src/UltimateCameraMod.V3/UltimateCameraMod.V3.csproj#L3)
  targets a Windows .NET/WPF application. No code/dependency adoption is proposed.

Inference: this is useful evidence that community tools can describe camera
parameters for particular game versions. It does not establish a per-frame
6DOF write path, engine view/projection override, frame ownership, independent
left/right world rendering, compatible temporal history or safe XR GPU handoff.
Those remain UNVERIFIED on our installed build. The review does not claim to
have ruled out every possible runtime path elsewhere in the repository.

## Author-documented photo-camera capability

The [Otis_Inf author documentation](https://opm.fransbouma.com/Cameras/crimsondesert.htm)
describes free movement/rotation and FOV control for a version-specific Steam
photo-camera tool. This is an author capability claim, not our test evidence.
It does not demonstrate two-eye gameplay or an open-source adapter that this
project can adopt. No paid/proprietary injector dependency is introduced.

## Next engineering proof

The external title-menu trace establishes a presentation baseline; see
[the actual record](../evidence/g1-title-presentation-2026-11-05.json).
Windows module metadata shows D3D11, D3D12/D3D12Core and DXGI loaded, which is
not enough to assign all world rendering to one API. The local Direct3D12 ETW
provider exists, but its game submission events have not yet been captured.

The next experiment must establish actual rendering/frame ownership and a
reviewed runtime camera-control point on the exact installed build. Require
near/far parallax from two world renders before advancing G2. Avoid treating
static FOV edits, UI diagrams, monoscopic texture copies or someone else's
photo-camera claim as a stereo proof. Any prospective integration method needs
its own technical review, reversible experiment and protection stop conditions.
