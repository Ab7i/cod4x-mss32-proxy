# iw3sp_mod -- deep-dive (read-only)

Clone: `tools\iw3sp_mod-ref\` (sparse not needed -- 134 MB, 7,622 files
with `--depth 1`). License **GPL-3.0**. Origin: based on the
**IW4x Client**. Repo: `gitea.com/JerryALT/iw3sp_mod`.

## 1. Repo layout

```
iw3sp_mod-ref/
  src/                            (C++ 80% mod code)
    Components/Modules/
      Gamepad.{cpp,hpp}           <- THE gamepad module, 2,355 lines
      Movement.cpp                <- analog movement integration
      Dvars.cpp                   <- cvar registration
      UIScript.cpp                <- UI script handlers
      AssetHandler/FastFiles/...  (asset / fastfile patching)
      ~50 other component modules
    Game/Functions.{cpp,hpp}      (iw3sp.exe addresses + structs)
    Game/Structs.hpp              (engine type definitions)
    Utils/                        (hooking helpers etc.)
    DLLMain.cpp                   (loader entry)
  iw3sp_mod_ff_src/
    raw/
      ui/         <- 70 .menu files (singleplayer)
      ui_mp/      <- .inc shared includes (menustyle, common_macro,
                     leftside_controls*, navcontrols)
      materials/, material_properties/, soundaliases/, video/, ...
    zone_source/  <- iw3sp_mod.csv (main zone) + per-language patch CSVs
  deps/                           (curl, ImGui, json, libtomcrypt, ...)
  tools/                          (build helpers)
  generate.bat                    (premake5 invocation)
  premake5.lua                    (build script)
```

Build is **Visual Studio** (premake5 → `iw3sp_dev.sln`). Not gcc/MinGW.

## 2. The menu source -- directly portable

```
iw3sp_mod_ff_src/raw/ui/
  controls.menu                  (in-game controls menu)
  options_controls_main.menu     (controls landing page)
  options_controls_settings.menu (general controls)
  options_gamepad_settings.menu  <-- gamepad page
  options_gamepad_controls.menu  <-- bind picker page
  options_gamepad_defaults.menu  <-- reset-to-defaults menu
  controls_buttons_set.inc       (button preset macro/include)
  controls_thumbstick_set.inc    (thumbstick preset macro/include)
  controls_common.inc            (shared controls bits)
iw3sp_mod_ff_src/raw/ui_mp/
  leftside_controls.inc          (side panel framework)
  leftside_controls_gamepad.inc  (gamepad side panel)
  leftside_controls_keyboard_mouse.inc
```

### Sample syntax -- `options_gamepad_settings.menu`

```
#include "ui/menudef.h"
#include "ui_mp/common_macro.inc"
#define OPTIONS_STYLE 1
#include "ui_mp/menustyle.inc"
#include "ui/choices_setup_common.menu"

#define ON_GPAD_ENABLED  when(dvarBool("gpad_enabled") == 1)
#define ON_GPAD_DISABLED when(dvarBool("gpad_enabled") == 0)

{
    menuDef {
        name        iw3sp_mod_gamepad_settings
        fullScreen  0
        rect        0 0 640 480
        focusColor  COLOR_FOCUSED
        onOpen      { focusFirst; close iw3sp_mod_options; ... }
        onESC       { close self; }

        #include "ui_mp/leftside_controls_gamepad.inc"

        CHOICE_SECTION_TITLE(21, "@IW3SP_MOD_LOC_MENU_GAMEPAD_OPTIONS")
        CHOICE_DVARYESNO    (22, "@..._GAMEPAD_ENABLED",
                             gpad_enabled,
                             uiScript "gamepadFirstExecIfEnabled")
        CHOICE_DVARFLOATLIST_VIS(23, "@..._BUTTON_STYLE",
                                 gpad_style,
                                 { "@..._XBOX" 0 "@..._SONY" 1 }, ;,
                                 ON_GPAD_ENABLED)
        CHOICE_DVARYESNO_VIS(24, "@MENU_AIM_ASSIST",
                             gpad_aimassist,
                             exec "setaimassist", ON_GPAD_ENABLED)
    }
}
```

This answers the menudef question conclusively:
- **Sliders/dropdowns:** `CHOICE_DVARFLOATLIST_VIS` (named options),
  `CHOICE_DVARFLOAT_LIVE` (numeric slider) etc. -- well-defined macros
  in `common_macro.inc`. Yes, dropdowns and 1-10 sliders are supported.
- **dvar binding:** menu items bind directly to a named dvar.
- **Visibility expressions:** `when(dvarBool(...))` -- conditional
  show/hide.
- **Actions:** `exec "<console_command>"` or `uiScript "<scriptName>"`.

These are the standard IW3 menudef macros (well-documented in the
mod-tools community wiki). Our prior worry ("can we express slider 1-10
/ dropdown?") is resolved -- yes, trivially.

## 3. Preset .cfg files -- directly portable as data

The same set of .cfg files we already saw inside `iw3sp_mod.iwd`:
- `buttons_default.cfg`, `buttons_lefty.cfg`, `buttons_tactical.cfg`
  (+ `_alt` variants).
- `thumbstick_default.cfg`, `thumbstick_southpaw.cfg`,
  `thumbstick_legacy.cfg`, `thumbstick_legacysouthpaw.cfg`.

These contain console commands -- `bind BUTTON_A "+gostand"`,
`bindaxis A_RSTICK_X VA_YAW MAP_LINEAR`, etc. Loaded by `exec
<file>.cfg` from the menu. The .cfg files only work if the engine
recognises `BUTTON_*` keynames and the `bindaxis` command.

## 4. `Gamepad.cpp` -- the engine port (the hard part)

2,355 lines. Implements an entire native gamepad subsystem against the
engine. Key functional blocks:

### XInput plumbing
- `GPad_UpdateDigitals`, `GPad_UpdateAnalogs`, `GPad_UpdateSticks`
  (reads `XINPUT_GAMEPAD`).
- `GPad_GetButton`, `GPad_IsButtonPressed/Released`.
- `Vibrate(leftVal, rightVal)` -> `XINPUT_VIBRATION`.

### Bind-system extension (engine-level)
- Registers extra keynames (`BUTTON_A` ... `DPAD_RIGHT`,
  `BUTTON_LTRIG`, `BUTTON_RTRIG`, `BUTTON_LSHLDR`, `BUTTON_RSHLDR`,
  `BUTTON_LSTICK`, `BUTTON_RSTICK`, `BUTTON_BACK`, `BUTTON_START`) via
  `CreateKeyNameMap()` and hooks
  `GetLocalizedKeyName_Stub` / `_Stub02`.
- Hooks `Key_SetBinding` (3 stubs), `Key_GetCommandAssignmentInternal`,
  `Key_WriteBindings` so the engine's bind/config-write path also
  understands gamepad keys.
- Adds the `bindaxis` console command (`Axis_Bind_f`,
  `Axis_Unbindall_f`) and corresponding axis-config writer.

### Per-frame integration
- `IN_GamePadsMove` polled inside `IN_Frame_Hk` (hooked at iw3sp:0x594913).
- `CL_GamepadButtonEvent` queues a `SE_KEY` event on a BUTTON_* key.
- `CL_GamepadEvent` (analog) routes through `Gamepad_BindAxis` -> the
  user's VA_* axis assignment -> usercmd.
- `CL_GamepadMove` produces a `usercmd_s` from sticks
  (this is the proper analog usercmd path -- our "Stage 3B usercmd
  hooking" item).
- Replaces the mouse path: `CL_MouseMove_Stub` (at 0x4402F7) +
  `CL_MouseEvent_Stub` (at 0x5947A8).

### Aim assist (full ported subsystem -- ~15 functions)
- Target acquisition (`AimAssist_GetBestTarget`,
  `AimAssist_GetPrevOrBestTarget`, `AimAssist_GetTargetFromEntity`).
- Lock-on (`AimAssist_IsLockonActive`, `AimAssist_ApplyLockOn`).
- Auto-aim (`AimAssist_ApplyAutoAim`, `AimAssist_UpdateAutoAimTarget`).
- Slowdown near targets (`AimAssist_IsSlowdownActive`,
  `AimAssist_CalcSlowdown`).
- Turn rates (`AimAssist_ApplyTurnRates`,
  `AimAssist_CalcAdjustedAxis`).
- Wires into `AimAssist_UpdateGamePadInput` (replaces the per-frame
  aim input path).

### Context-sensitive use button
- `Gamepad_ShouldUse` + `Player_UseEntity_Stub` (at 0x4F92D7) --
  context-sensitive "+activate" handling, like the console "X" button.

### Other touches
- Konami-style cheat detector (`GamePadCheat`).
- Rumble after game events.
- `gpad_*` cvar registration via `CG_RegisterDvars_Hk`.
- Hint UI ("Press A to ...") through `UpdateGamepadHint`.

### Bottom line on the engine port

This is a **proper engine extension** that re-enables the gamepad
subsystem the IW3 engine inherited from console ports. The PC retail
binary has the SUPPORT compiled in but disabled / unwired; iw3sp_mod
patches it back on and routes XInput into it.

For our project the implication is:
- **iw3mp.exe is the same engine** -- the gamepad code paths exist
  there too. CoD4x_Client_pub already patches iw3mp.exe in many
  places, so the toolchain is in place.
- **But every hook address is iw3sp-specific.** ~30+ hook sites; each
  needs the equivalent address in iw3mp.exe. That is real RE work --
  either by binary-diffing the two exes (they share most code), by
  looking for matching byte signatures, or by hand in a disassembler.
- The C++ port itself is mechanical once addresses are known.

## 5. What is directly portable vs what needs work

| Layer | Effort | Notes |
|---|---|---|
| .menu UI files (gamepad pages, controls landing) | LOW | C-preprocessor includes; need to ship `common_macro.inc`, `menustyle.inc`, `choices_setup_common.menu` and our localized strings. |
| Preset .cfg files (buttons_*, thumbstick_*) | LOW | Pure config; depend on `BUTTON_*` keynames + `bindaxis` command -- only useful AFTER engine port, OR after rewriting them to use K_JOY1-16 + the existing bind system. |
| Button-glyph .iwi assets (Xbox + PS3) | LOW | Already extracted from `iw3sp_mod.iwd` (we have them). |
| `gpad_*` cvar registration | LOW | Add via `Cvar_RegisterBool/Float` in our `gamepad.c` (we already do this for `cl_gamepad_*`). Rename to match iw3sp_mod's names if we want config compatibility. |
| Localized strings (`@IW3SP_MOD_LOC_*`) | LOW-MED | Either ship their localization, or replace `@TOKEN` references with English literal strings in our menu copy. |
| Gamepad.cpp body (XInput poll, sticks, buttons) | MED | The C++ logic is portable; we already have most of it (Commit 1-3). Their version is more polished. |
| BUTTON_* keyname extension | HIGH | Engine hook into the keyname map + Key_SetBinding/GetAssignment paths. Address discovery required for iw3mp.exe. |
| `bindaxis` command + virtual-axis mapping | HIGH | Same: engine hook + new command registration. |
| Aim assist port | HIGH | ~15 functions; needs MP context (`AimInput`/`AimOutput`/`AimAssistGlobals` already referenced in our `cl_keys.c`!). |
| Usercmd analog movement (`CL_GamepadMove`) | HIGH | This is our "Stage 3B usercmd" goal -- needs the `CL_MouseMove` hook address in iw3mp.exe. |
| Engine hook addresses (~30 sites) | HIGH | Binary-diff or signature-match iw3sp vs iw3mp. Slow but bounded. |

## 6. License hygiene (GPL-3, must do)

If we copy any iw3sp_mod content into our project:
- Every copied or derived file carries a top-of-file attribution:
  source repo (`gitea.com/JerryALT/iw3sp_mod`), original author
  (JerryALT), license (GPL-3.0), and what we adapted.
- Commit messages call out the borrowing explicitly.
- CoD4x_Client_pub is AGPL-3.0 -- compatible (AGPL = GPL + network
  clause), so the licensing direction works.
- For the PR stage, we will need a clear NOTICE / CREDITS file
  listing all upstream attributions.

## 7. Recommended pivot plan -- two-phase

The bigger picture changes. Instead of authoring `gamepad.menu` from
scratch and rebuilding `ui_mp.ff` (the previous Stage 3B plan), do:

### Phase 3B' -- ship the UI, keep our existing input plumbing (1.5-2 days)

Goal: a proper "Gamepad" page in the Options menu, configurable, with
sliders and a button-style toggle and an aim-assist toggle (no-op for
now). Reuses iw3sp_mod's menudefs as data; no engine port yet.

1. Copy these files from iw3sp_mod_ff_src/raw/ into our work area:
   - `ui/options_gamepad_settings.menu`
   - `ui/options_gamepad_controls.menu`
   - `ui/options_gamepad_defaults.menu`
   - `ui/controls_buttons_set.inc`, `controls_thumbstick_set.inc`,
     `controls_common.inc`
   - `ui_mp/leftside_controls_gamepad.inc`, `leftside_controls.inc`,
     `leftside_controls_keyboard_mouse.inc`, `navcontrols.inc`
   - The `common_macro.inc` / `menustyle.inc` / `choices_setup_common.menu`
     they include (resolve transitively).
   - Localized strings file (or in-line English text).
2. Rewrite `buttons_*.cfg` and `thumbstick_*.cfg` to use **`JOY1`..`JOY16`**
   (our current keynames) rather than `BUTTON_*` -- they ship loadable
   as-is via `exec`.
3. Rename our `cl_gamepad_*` cvars to `gpad_*` to match the menu
   bindings -- one search/replace in `gamepad.c`. (Or alias both names.)
4. Build a small additive patch zone `cod4x_gamepad.ff` via the SDK
   linker (we proved this works) listing just the new/modified menus.
5. Deploy alongside CoD4x's existing patch zones. **No `ui_mp.ff`
   touched.** No CoD4x UI customisations lost.
6. Test: from the Options menu, "Gamepad" entry opens the page;
   toggling cvars from the page actually changes our XInput behavior.

### Phase 3D -- proper engine port (later, ~1.5-2 weeks)

Goal: BUTTON_*/bindaxis/gpad_* as native engine concepts in iw3mp.exe,
plus the aim-assist port, plus analog usercmd movement.

1. Read iw3sp_mod's Game/Functions.cpp for the iw3sp address map.
2. For each hook site, find the equivalent function in iw3mp.exe
   (binary-diff or pattern-match). Build the iw3mp address table.
3. Port `Gamepad.cpp` into CoD4x_Client_pub (likely as a new C++
   compilation unit, or rewritten in C to match CoD4x style).
4. Re-author `buttons_*.cfg` to use real BUTTON_* keynames once
   supported.
5. Aim assist last -- biggest piece, needs careful MP testing.

## 8. Risks / unknowns

- iw3mp.exe addresses must be discovered for ~30 hook sites; some
  functions may not exist or may differ between SP and MP. Need a
  spike to estimate before committing to Phase 3D.
- Fastfile-version compatibility: iw3sp_mod.ff is built by their
  toolchain; our SDK ui_mp.ff was `[ver. 5]`. Need to confirm the
  patch zone we build is loadable by iw3mp.exe.
- `cod4x_patchv2.ff` may already override `controls.menu` /
  `options_controls_main.menu`. Adding a "Gamepad" entry to the
  controls landing might collide. Pre-flight: extract just the
  asset-name list from `cod4x_patchv2.ff` to know what it touches.
- GPL-3 hygiene: attribution everywhere; verify CoD4x's AGPL is
  preserved in any combined module.
- MP-specific concerns: usercmd timing, server-side anti-cheat (does
  the engine flag synthesized movement?), how the engine's bind system
  serialises `BUTTON_*` vs keyboard binds into `config_mp.cfg`.

## 9. Bottom line

iw3sp_mod gives us:
- A **proven, open, GPL-3 reference** for native CoD4 controller
  support, including aim assist.
- **Portable UI** (menus + presets + glyph assets) we can adopt with
  one day of work to land a real "Gamepad" page in the Options menu.
- A **clear roadmap** for the deeper engine port to MP -- bounded,
  with each piece visible in their source.

Recommendation: approve **Phase 3B'** (UI port using existing K_JOY
plumbing) as the next executable step. Defer Phase 3D until 3B' is
shipped and tested -- by then we will have a working menu and a
concrete sense of which engine-level features matter most.
