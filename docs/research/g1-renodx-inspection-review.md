# RenoDX draw/resource inspection: pinned source review

Reviewed at bc0ead39917634e95c534f7e8cfb59ab82880747 on 2026-11-06.
The [source inventory](renodx-inspection-source-inventory.json) fingerprints
retrieved files and declared dependency Git links. The root is MIT; its full
notice is retained in docs/licenses/RenoDX-MIT.txt. This is not a complete
dependency audit. No addon, bridge, shader replacement or upstream code was
built, adopted or executed. No game, headset or user configuration was accessed.

## Concrete interface

The [devkit documentation](https://github.com/clshortfuse/renodx/blob/bc0ead39917634e95c534f7e8cfb59ab82880747/docs/DEVKIT_MCP.md)
describes an in-process ReShade addon plus a local named-pipe bridge. Its
snapshot/get-draw/get-shader operations are a concrete alternative observation
lead. The [draw serializer](https://github.com/clshortfuse/renodx/blob/bc0ead39917634e95c534f7e8cfb59ab82880747/src/addons/devkit/mcp/draw_summary.hpp)
includes pipeline shader hashes and constant-buffer handle, offset, size,
register slot/space and shader-use metadata. These could associate a resource
with a particular recorded draw without a literal archive-name relationship.
They do not identify a camera buffer or establish camera control.

## Limits found in the selected implementation

The [review record](../evidence/g1-renodx-inspection-review-2026-11-06.json)
retains exact source locations and separates facts from unverified behavior.

| Area | Source observation | Qualification consequence |
|---|---|---|
| Shader stage | addon.cpp:5039-5048 uses pixel-stage resource reflection for graphics. Lines 5126-5132 exclude descriptor-table ranges without pixel visibility. | Do not interpret a missing vertex-only table binding or a false shader-use flag as absence of a camera consumer. Direct/pushed CBV coverage is a separate path. |
| Descriptor ranges | Lines 5181-5207 use reflected slots or a fallback of at most 256 slots per range. | A returned subset is not proof of complete table coverage. |
| Submission | Lines 9122-9145 record submission order internally; lines 9272-9278 retain unknown-submission entries. MCP DrawSummary omits that internal marker. | A listed draw alone is not proof of submission or GPU completion. Sorting does not supply a fence or a complete frame association. |
| Buffer bytes | resource_analysis.hpp:652-656 accepts only 2D texture views. constants.hpp caches optional CPU map/unmap/update observations and defaults capture off. | The texture endpoint is not constant-buffer readback. CPU cache/history is not a per-draw GPU byte snapshot; persistent-map writes need separate evidence. |
| Startup effects | The devkit reads settings/boot/live assets and enables replacement infrastructure. The separate Crimson Desert addon registers pre-draw VRS override callbacks. | Full startup and the game addon cannot be treated as passive metadata observation. Isolate and verify actual configuration before owned execution. |
| Transport | The named pipe rejects remote clients, has a default 4 MiB message cap and grants local Everyone read/write by its default DACL. | Local transport is not exclusive target/owner identity. A controller still needs explicit process, endpoint and device checks. |

The get-shader endpoint defaults to summary metadata and permits an explicit
summary view. Disassembly, decompilation, shader/resource dumping, cloning and
replacement remain distinct operations; none was requested or executed here.
The documented MCP surface also includes mutation tools, so its availability
is not itself a read-only operating contract.

## Next owned qualification

First pin/audit the required tool inputs and qualify a local loading method on
an explicitly identified owned presenting fixture, outside the game. Use
isolated configuration, empty boot/live paths and fresh output directories;
verify that no automatic replacement or dump occurs. Target/pipe/device identity,
timeout and disconnect cleanup are part of that qualification. Response
pagination is not evidence of a backend capture budget; bound the owned
workload and qualify collection limits before any larger target. No global MCP
registration or game-folder change is justified by this static review.

Exercise vertex-only and pixel-only resources, root and table CBVs, descriptor
rebinding, command-list reset/reuse and a recorded-but-unsubmitted list. Require
the actual exported data to preserve the distinctions needed by the experiment;
keep missing stage/submission information unknown. Compare any eventual value
observation with a known owned producer and submitted GPU work, including
persistent mappings. Protocol-shaped mock JSON alone would not qualify tracking.

This leads toward a bounded draw/resource association experiment, not an
immediate current-game installation. A successful owned result would still
require a reviewed current-build loading and evidence method. The old game
addon's shader hashes and VRS override do not establish current camera matrices,
extra world-view scheduling or temporal-history ownership. ADR 0003 retains
G2's hold. M0's code/package remain independent; headset tests remain deferred.

A later [selected owned cache-test qualification](g1-owned-reshade-descriptor-method.md)
executes only the upstream test app/addon with pinned ReShade in an isolated
workspace case. It does not execute the full devkit or the game addon, and does
not upgrade this static review's unverified draw/camera findings.
