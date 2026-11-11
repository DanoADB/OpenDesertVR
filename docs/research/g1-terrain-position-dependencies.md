# Terrain position input experiment

Live33 rules out state-tracker capacity exhaustion at two terrain capture
points. Both reject DEFAULT-buffer readback because there is no retained
source-specific state observation. This does not identify why the observation
is absent. No state is inferred from the CBV binding and no copy is forced.

The next experiment follows the already identified terrain vertex shader's
input values to its four SV_Position output components, entirely in process.
The existing helper first resolves the exact bounded CBV declaration, handles
and static legacy loads. A second pass follows scalar SSA value operands
backward from each output store to individual extracted CBV components.
The output contains numeric row indices and word indices (4*row+component).
It contains no shader text, local names, payload or game assets.

The helper requires one vertex entry, one function, a unique four-component
position signature and one supported store per component. Missing or dynamic
stores, duplicate definitions, unresolved scalar values, unknown operations,
helper calls and unsupported aggregate uses leave the result unverified.
Limits remain one MiB of text, 8,192 definitions and 65,536 dependency edges.
Phi/select alternatives form conservative unions; loops terminate through a
visited set. These are potential SSA value dependencies, excluding control
and memory dependencies. Empty sets do not establish that a field cannot
influence rendering through those excluded mechanisms. A row on a potential
path is not proof of executed contribution or a camera matrix.

Owned tests check independent component indices, constants, dead select arms,
phi joins, unresolved phi values, unknown calls, memory loads, duplicate
definitions, multiple functions and duplicate/missing/dynamic output stores.
Actual SDK SM6.0 and SM6.6 shaders must report the expected four different
component indices for their last-row position values, including shaders with
additional unrelated resources. Dynamic target loads remain rejected.

Observer34 only exports this numeric classification. Its first game capture
disables constant copies (ODVConstantBytes=0), clip effects and GPU-work queries.
It keeps the exact observed terrain hash and b18/space35 role. Original input
buffers, descriptors, PSOs and game rendering are unchanged. Any subsequent
private-shader component perturbation requires a separately qualified identity,
modified and restored control. It has not run in the game yet. Independent eye
rendering, whole-world coverage, calibrated camera transforms and game OpenXR
delivery remain NOT_RUN.

## First live result and scalar control

Two live34 captures report rows18–21 with component columns matching the four
position outputs. Word84 (row21.x) has a potential scalar value path to X and
no such path to Y/Z/W. Other rows also contribute; this is not matrix identity
or a complete influence analysis. No source words or raw shader text are read
out of the game.

Observer35's separate opt-in ODVClipProbeCameraField=1 requires one exact
float32 extraction for b18/space35 row21.x and the observed X-only value-path
classification. It changes that scalar by0 or±0.125 in private shader code.
Other extracted components and all original buffer contents are unchanged;
every consumer of the selected scalar sees the perturbed value. Control and
memory influences remain outside the analysis, so this does not promise that
only one final output can change. The original root/PSO source, descriptors,
indirect arguments/counts, predicate and single-forwarding path are preserved.
The option is incompatible with the older large/collapse probes and is read
once before nonzero control publication. The existing short control lease and
mode0 restoration apply. Game copies and work queries stay disabled.

Owned SM6.0/6.6 controls exercise legacy and stream PSO creation, both compiler
pairs, per-pixel identity/left/right/restoration checks, near/far disparity and
unchanged2816-byte source data. A missing selected extraction and an X/Y-shared
value path reject before GPU recording. The initial positive test shader used
a constant-array lookup that compiled to a memory load; the helper correctly
rejected it. Equivalent scalar vertex math brings that owned control inside
the documented SSA scope without weakening rejection. The candidate has not
yet been perturbed in the game. Its delta has no calibrated world/IPD units.
