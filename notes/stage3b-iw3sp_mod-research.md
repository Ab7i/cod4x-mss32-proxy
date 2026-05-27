# iw3sp_mod -- reference work for native controller support

The user discovered an existing CoD4 mod that already implements native
controller support. **Open-source, GPL-3.0, with proper engine
extensions (`BUTTON_*`, `bindaxis`, `gpad_*` cvars), preset schemes
(default/lefty/tactical, default/southpaw/legacy/legacy-southpaw), and
PS3+Xbox button icon assets.** This is the gold-standard reference for
our work.

## 1. Where it is on disk

| Path | Size | SHA256 |
|---|---|---|
| `D:\Cod4Project\game-test\iw3sp_mod.exe` | 3,248,640 | `EDED51AC...A6B` |
| `D:\Call of Duty_OG Games\COD4\cod4_game_files\iw3sp_mod.exe` | 3,248,640 | (same file, same mtime) |

For comparison: stock `iw3sp.exe` is 3,035,136 bytes -- the mod is
**213 KB larger** -> they added meaningful native code to the engine.

Hashes:
- `iw3sp_mod`: `EDED51AC28C261FCB0E8C66D0294847323F53E3C6D8F2D2A21750D535F1DAA6B`
- `iw3sp`    : `7AEC4E2E3FA9ED188809D09FC08149C9024F6208E2F296149FFD0FC90757A2ED`

## 2. Support files alongside (`game-test\iw3sp_data\`)

```
iw3sp_data\
  assets\
    console_logo.bmp           (146 KB)
    splash_logo.bmp            (1.4 MB)
  miles\                       (Miles sound DLLs)
  zone\
    iw3sp_mod.ff               (1.16 MB  -- mod's fastfile, contains the menudefs)
    english\english_iw3sp_mod_patch.ff   (44 KB)
    french|german|italian|portuguese|russian|slovak|spanish\..._patch.ff
  iw3sp_mod.iwd                (590 KB -- assets, see below)
  localized_<lang>_iw3sp_mod.iwd  (per-language string tables)
  settings.json                (lang setting)
```

## 3. `iw3sp_mod.iwd` contents -- 103 entries

**Xbox button icons:** `button_a/b/x/y.iwi`, `button_back/start.iwi`,
`button_lshldr/rshldr.iwi`, `button_ltrig/rtrig.iwi`,
`button_lstick1[_pressed]/rstick1[_pressed].iwi`,
`dpad_up/down/left/right.iwi`.

**PS3 button icons (full set):** cross / circle / square / triangle,
`l1/l2/l3[_pressed]`, `r1/r2/r3[_pressed]`, `ps3_back/start`,
`dpad_ps3_*`, plus the full `ps3_controller_lines_classic_sp.iwi`
(350 KB layout diagram), `ui_button_ps3_dpad_64x64.iwi`,
`logo_iw3spmod.iwi`.

**Preset config files** (THIS is the AAA-console-CoD pattern):
- Button layouts: `buttons_default.cfg`, `buttons_default_alt.cfg`,
  `buttons_lefty.cfg`, `buttons_lefty_alt.cfg`,
  `buttons_tactical.cfg`, `buttons_tactical_alt.cfg`.
- Thumbstick layouts: `thumbstick_default.cfg`,
  `thumbstick_legacy.cfg`, `thumbstick_southpaw.cfg`,
  `thumbstick_legacysouthpaw.cfg`.
- Options stash: `options_iw3sp_mod.cfg`, `options_iw3sp_mod_set.cfg`,
  `iw3sp_mod_devgui_maps_sp.cfg`.

### Sample -- `buttons_default.cfg`

```
set gpad_buttonsConfig "buttons_default"
bind BUTTON_START togglemenu
bind BUTTON_BACK togglescores
unbind BUTTON_RSHLDR  ... (unbind all)
bind BUTTON_RTRIG  "+attack"
bind BUTTON_LTRIG  "+speed_throw"
bind BUTTON_RSHLDR "+frag"
bind BUTTON_LSHLDR "+smoke"
bind BUTTON_RSTICK "+melee"
bind BUTTON_LSTICK "+breath_sprint"
set gpad_button_rstick_deflect_max  1.0
set gpad_button_lstick_deflect_max  1.0
bind BUTTON_A "+gostand"
bind BUTTON_B "+stance"
bind BUTTON_X "+usereload"
bind BUTTON_Y "weapnext"
bind DPAD_UP    "+actionslot 1"
bind DPAD_DOWN  "+actionslot 2"
bind DPAD_LEFT  "+actionslot 3"
bind DPAD_RIGHT "+actionslot 4"
```

### Sample -- `thumbstick_default.cfg`

```
bindaxis A_LSTICK_X VA_SIDE     MAP_SQUARED
bindaxis A_LSTICK_Y VA_FORWARD  MAP_SQUARED
bindaxis A_RSTICK_X VA_YAW      MAP_LINEAR
bindaxis A_RSTICK_Y VA_PITCH    MAP_LINEAR
```

This is a **completely different (and better) architecture** than our
current K_JOY1-16 keyboard-event approach. They have proper engine
support for:
- `BUTTON_*` bind names (no shoehorning into K_JOY keycodes).
- `bindaxis` -- analog axes bound to **virtual axes** (`VA_SIDE`,
  `VA_FORWARD`, `VA_YAW`, `VA_PITCH`) with mapping modes (`MAP_LINEAR`,
  `MAP_SQUARED`).
- `gpad_*` cvars (deflect_max, buttonsConfig preset name, ...).
- Preset SWITCHING via `exec buttons_lefty.cfg` -- exactly the
  console-CoD "Combat preset" picker.
- Aim assist (mentioned in README; binary likely contains the math).

## 4. Source -- open and inspectable

**Repository:** `gitea.com/JerryALT/iw3sp_mod`. License **GPL-3.0**.

Mirror (RTX-Remix fork): `github.com/xoxor4d/iw3sp-mod-rtx`.

Origin: **based on the IW4x Client** (the long-standing community MW2
client mod -- which has had native controller support for years).

Repo layout:
- `src/` -- code (predominantly **C++ 80%**, with GSC, Pawn, C, Lua).
- `iw3sp_mod_ff_src/` -- **almost certainly the menudef + zone source
  for `iw3sp_mod.ff`** (the goldmine: the controller-options menu,
  in-game prompts, button-glyph widgets).
- `deps/` -- dependencies.
- `tools/` -- build utilities.

Licensing relevance for us: CoD4x_Client_pub is **AGPL-3.0**. AGPL-3 is
GPL-3 + a network clause; **GPL-3 code can legally be incorporated into
an AGPL-3 project**. So we can study and adapt iw3sp_mod source.

Caveats:
- It's for `iw3sp.exe` (singleplayer); our work targets `iw3mp.exe`
  (multiplayer). The CoD4x forum announcer notes exactly this:
  "Currently its singleplayer only but I imagine someone could take the
  code and make it work in multiplayer." We are that "someone."
- The menudef + cvar + bind systems are SHARED between SP and MP -- the
  UI/preset assets port directly. The engine extensions
  (`BUTTON_*` keynames, `bindaxis` command) are SP-only patches; we
  would need to apply the equivalent patches to `iw3mp.exe` from
  `cod4x_021.dll`. That is exactly the kind of engine-side patching
  CoD4x already does.

## 5. What this means for our project

We are currently doing:
- XInput poll -> SE_KEY events on K_JOY1..K_JOY16 -> keyboard bind system.
- Right stick -> CL_MouseEvent (mouse-equivalent path).
- Left stick -> arrow-key events (digital movement).

iw3sp_mod does:
- A proper gamepad subsystem with `BUTTON_*` bind keynames, `bindaxis`
  for analog, `gpad_*` cvars, preset .cfg files for layouts, and
  in-game UI for picking them. Plus aim assist.

The patterns we have built work, but they are an MVP. iw3sp_mod
demonstrates the production-grade architecture.

## 6. Risks and unknowns

- We have **not** read `iw3sp_mod_ff_src` yet -- the menudef source
  is "almost certainly" there, but unconfirmed until the repo is cloned
  and inspected.
- iw3sp_mod's engine extensions (`BUTTON_*`, `bindaxis`) are
  binary-patched into `iw3sp.exe`. Replicating them in `iw3mp.exe`
  via `cod4x_021.dll` is a real engineering task -- the *patches*
  themselves are in their C++ source and can be read, but adapting
  them to MP needs careful work.
- License: GPL-3 is copyleft. Any code we copy from them must be
  attributed and our derived work must remain under a GPL-compatible
  license. AGPL-3 (CoD4x's license) qualifies. Just be deliberate
  about attribution.
- SP/MP engine differences: some MP-specific things (anti-cheat,
  network usercmd) need consideration that SP did not.

## 7. Recommendation -- pause Stage 3B and study iw3sp_mod first

**Yes, study before continuing.**

Specifically:
1. **Clone `gitea.com/JerryALT/iw3sp_mod` (sparse if huge).** Read
   `iw3sp_mod_ff_src` to see the controller menudefs and presets in
   their real form. Read `src/` for the engine patches (`BUTTON_*`,
   `bindaxis`, `gpad_*`, aim assist).
2. **Decide whether to pivot.** Two real options:
   - **Pivot:** port their gamepad subsystem (BUTTON_*/bindaxis/gpad_*)
     to MP via CoD4x_Client_pub; reuse their .cfg presets and the
     controller-options menu. This is the right long-term answer.
   - **Borrow only the UI:** keep our K_JOY1-16 plumbing, but reuse
     their menudef + button-glyph assets for the gamepad page. Faster
     but architecturally inferior.
3. **Either way, Stage 3B as previously planned (`gamepad.menu` from
   scratch + patch zone via mod tools) is no longer the right next
   step.** iw3sp_mod gives us a real, working precedent to learn from.

The few hours of study will save days of guesswork.

## 8. Scope note

No mod was run. No file was modified. No download performed beyond
`.iwi` extraction in a prior session. Investigation only.
