# Camera-buffer observer correction

`all-graphics-cbvs.patch` applies to RenoDX
`bc0ead39917634e95c534f7e8cfb59ab82880747` (`src/addons/devkit/addon.cpp`).
The upstream code is MIT, Carlos Lopez; retained notice: `docs/licenses/RenoDX-MIT.txt`.
The original reference tree and unmodified observations remain intact.

Two changes expose graphics CBV ranges beyond pixel visibility and enumerate
their bounded slots independently of pixel-only shader reflection. The existing
256-slot fallback cap remains. Shader replacement, buffer contents and GPU
commands are not changed by this patch. `devkit-cbv-input-lock.json` pins the
separate prepared source tree and patch; private build version is
`bc0ead3-cbv-observer-1`. No external source/tool is installed into M0.

This is a narrowly scoped observer qualification candidate, not a game adapter.
The exported CBV map still cannot distinguish different resources at the same
register/space across shader stages. It still omits submission markers and does
not capture per-draw GPU constant bytes. Those limitations must remain explicit
in any game-facing experiment. Loading into the game is not qualified by a
successful owned render alone.

`bounded-camera-observer.patch` is a separate combined patch against the same
upstream revision, prepared as `bc0ead3-bounded-observer-2` with
`devkit-bounded-input-lock.json`. It retains at most 128 draw/dispatch/copy
records in a snapshot and exports `captureRecordLimit`, `captureTruncated`,
`submissionKnown` and `submissionOrder` on each returned row. Truncation is
finalized when the snapshot closes. The controller must wait for completion
before reading it. The cap is a prefix, not a representative or complete frame.
It bounds retained records, not every internal cache or total process memory.
Submission means the existing execute callback associated that command list;
it does not establish GPU completion. No rendering calls are suppressed.
