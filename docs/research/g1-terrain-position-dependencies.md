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
