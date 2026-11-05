# G1 isolated Steam development-context experiment

Status: reviewed method; game execution NOT_RUN at preparation time.

The first direct RenderDoc launch connected, then lost its target before any
frame request. Windows process metadata subsequently identified Steam as the
parent of the replacement game process. That supports a handoff hypothesis;
it does not prove a particular Steam API call or protection caused the failure.
The original attempt is preserved in the evidence directory.

Valve's [Steamworks initialization documentation](https://partner.steamgames.com/doc/sdk/api#initialization_and_shutdown)
describes steam_appid.txt as a development hint, searched in the working
directory, that identifies an app when launched from a debugger. Its
RestartAppIfNecessary documentation explains the normal client relaunch and
the development-file exception. Initialization still requires a running Steam
client, matching user context and an owned app license. These are vendor
development instructions, not proof of publisher permission or compatibility.

## Scoped experiment

Create only a fresh ignored out/g1/frame-* directory containing the actual
installed app ID 3321460 in steam_appid.txt. Use that directory as the working
directory for the exact fingerprinted installed executable. RenderDoc's
[documented launch API](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/api/replay/renderdoc_replay.h)
accepts an explicit working directory. The game executable, arguments,
capture options and tool fingerprint remain the same as the first experiment.
The production adapter and portable package are unaffected.

No game-directory file, Steam launch option, registry entry, global environment,
Steam client, authentication response or license check is modified. No debugger
patch, API return override, protection setting, child/global hook or late attach
is introduced. The App ID is the installed game, not another title. This method
uses a documented development context and does not attempt to defeat Steam or
third-party authorization/anti-tamper checks.

Require the existing running-game guard and exact executable/tool checks. The
capture script accepts this context only with the fixed App ID file inside its
own case directory. Observe the title menu before requesting one frame. If the
game refuses this working directory, exits, hands off again, reports a license
or protection error, or never initializes D3D12, preserve that result and stop
this method. Do not change binaries, patch checks or retry with altered app IDs.
Handle security/authentication prompts manually. Close the experiment normally
and retain local evidence without committing proprietary captures.

Success requires a readable D3D12 frame and matching saved-copy hash. A captured
title screen still does not prove a world render pass, camera control, gameplay
stereo or G2. Any later camera/render intervention requires its own review and
near/far geometrical proof from independent world views.
