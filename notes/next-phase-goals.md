# Controller support -- phased roadmap

The staged plan beyond what is already shipped.

## Done

- **Stage A** -- `cl_gamepad_*` cvars (all `CVAR_ARCHIVE`).
- **Commit 1** -- XInput probe + button edge logging.
- **Commit 2** -- buttons + triggers wired to the engine via `Com_QueueEvent`.
- **Stage 3A** -- analog sticks: right stick -> view via `CL_MouseEvent`,
  left stick -> 8-way digital movement via arrow-key events, plus
  release-on-disconnect for every held input.

## Stage 3A.5 -- composite button command (current)

- Add a `+gp_use_reload` / `-gp_use_reload` command pair: one button
  fires `+activate` AND `+reload` together, releasing each separately.
  Mirrors the console-CoD "use / reload / skip-killcam" Square button.
- Bound by the user, e.g. `bind AUX3 +gp_use_reload`.

## Stage 3B -- usercmd hooking (true analog movement)

- The left stick is currently digital (8-way arrow keys). For real
  analog movement, hook the usercmd build and set `forwardmove` /
  `rightmove` directly from the stick magnitude.

## Stage 3C -- menu UI

- A "Gamepad" page in the Controls menu, binding to the `cl_gamepad_*`
  cvars (this is "Stage B" of the original two-stage A/B plan).

## Cleanup owed (see TODO.md)

- Tighten the loose `cl_gamepad_sens_*` / `deadzone_*` cvar ranges.
- Remove the temporary `cl_gamepad_debug` logging once sticks + triggers
  are settled.
- Trigger hysteresis fix if drift is confirmed.

## Deferred from Stage 3 (gamepad port -- decisions 2026-05-26)

### Discord rich-presence

- **Source:** `tools/iw3sp_mod-ref/src/Components/Modules/Discord.{cpp,hpp}`
- **What it does:** publishes "playing CoD4 Multiplayer" + map/mode/score
  to the user's Discord status (GameSDK or RPC).
- **Why deferred:** independent of controller support; needs Discord
  SDK integration; can be added later without breaking anything in
  Stage 3.
- **Estimated effort:** ~1 session once Stage 3 is shipped.
- **Open questions before porting:**
  - GameSDK (newer, deprecated by Discord) vs RPC (older but still
    supported) -- pick first.
  - App ID: create a fresh Discord application under `Ab7i`.
  - Privacy toggle: a `cl_discord_rpc` cvar (default 0?) so users opt in.

### IW Code easter-egg cheat

- **Source:** `Gamepad.cpp` lines 1820-1897 + `GamePadCheat` struct
  in `Gamepad.hpp` lines 7-17 (~78 source lines).
- **What it does:** in SP, pressing a 10-button code unlocks hidden
  content (Konami-style).
- **Why skipped:** SP-only feature; no meaning in competitive MP; keeps
  the first port lean.
- **Revisit if:** community asks for a hidden-content / easter-egg
  feature in MP later.

## Stage 3.5 polish (inside Stage 3 plan; listed here as reminder)

- Find the correct byte-offset inside iw3mp `0x4237B0` for the
  reload-hint patch (Stage 2 confirmed the function via 4/4 strings
  but offset 0xE0 invalid).
- Resolve iw3mp twin of `0x5947A8` (CL_MouseEvent_Stub) via the
  `0x43F920` callee chain.
- Resolve `Player_UseEntity` iw3mp twin (`0x4F92A0` in src).
- Resolve `UI_RefreshStub` iw3mp twin (`0x565360` in src).
