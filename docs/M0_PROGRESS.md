# M0 Progress & Evidence

## Implemented
- Native C++20 `odv_math` library with coordinate convention documented in the header.
- Independent eye pose, quaternion normalization, view matrices and asymmetric OpenXR-style projection with D3D depth mapping.
- Headless tests checking IPD, head orientation, camera-space origin, clipping depth, disparity falloff and invalid inputs.
- VS2022 x64 debug/release presets and Windows CI workflow.

## Not yet implemented
- OpenXR loader, runtime discovery or D3D12 frame submission.
- DirectX 12 rendering testhost, diagnostics, camera integration, packaging.
- Real headset tests. No game VR capability exists yet.

## Developer checks
```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --config Debug
ctest --preset windows-msvc-debug -C Debug
```

For portable Linux Ninja debug:
```sh
cmake --preset portable-debug
cmake --build --preset portable-debug
ctest --preset portable-debug --output-on-failure
```

OpenXR uses right-handed axes (+X right,+Y up,-Z forward); matrices are row-major with column vectors; projection depth is D3D [0,1]. More details: `docs/OpenDesertVR_Technical_Specification.md`.
