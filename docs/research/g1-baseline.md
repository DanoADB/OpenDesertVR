# G1 baseline and integration hold — 2026-11-05

Core M0 headset acceptance passed on the tested Quest Pro configuration.
G1 begins with metadata and public documentation, not a game adapter.

## Observed local baseline

The installed Steam manifest identifies app 3321460, Crimson Desert Enhanced,
build 26448710. The main executable's version resource is 1.0.0.3549;
these identifiers describe different things and are not reconciled into an
invented patch version. Windows reports a valid Authenticode signature.
The executable SHA256, size and sanitized metadata are in
[the evidence record](../evidence/g1-baseline-2026-11-05.json).
No executable was launched, modified, disassembled or instrumented. No game
memory, proprietary payload, account identifier or absolute install path is
published. A local installation does not itself establish ownership or mod
permission.

## Primary-source review

The [publisher's Steam EULA](https://store.steampowered.com/eula/3321460_eula_0),
effective March 19, 2026, Article 5 restricts modification/reverse engineering
(subject to applicable-law exceptions), unapproved programs affecting normal
operation, interference with protections and unauthorized data collection.
This is evidence of a permissions issue, not a legal determination about every
possible mod. No publisher approval or applicable exception has been established
for our proposed adapter.

The [official store listing](https://store.steampowered.com/app/3321460/Crimson_Desert_Enhanced/)
discloses Denuvo Anti-tamper and lists DirectX 12. These are publisher/store
claims, not local instrumentation results. Anti-tamper does not establish an
anti-cheat implementation. Network behavior, actual rendering/presentation and
camera access remain UNVERIFIED.

## Decision under the project boundaries

**HOLD live integration.** The specification requires a permitted approach
before hooks and forbids protection bypass. A free/open-source license on our
code, ordinary game ownership or an upstream mod's existence does not resolve
the permission issue. No hook, injection, camera offset or bypass is proposed.

To reopen live integration, obtain publisher permission covering the intended
camera/render extension, or establish a specifically reviewed permitted route
consistent with the project boundaries. Then record its scope and restrictions,
identify any documented extension API, and plan a minimal reversible experiment.
Do not silently treat an unresolved permission question as approval.

If a permitted route is established, first observe ordinary DX12 presentation
with approved tooling and explicit stop conditions. Establish frame ownership,
camera control, temporal effects and actual two-eye world rendering separately.
Texture copies alone cannot establish G2; see the
[upstream stereo audit](stereo-pipeline-audit.md). If no permitted route exists,
record a stop/redesign decision. No universal-game VR capability is established
by M0 or this baseline.
