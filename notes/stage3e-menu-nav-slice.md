# Phase 3-E.0 — menu-navigation slice feasibility (read-only)

_Written 2026-05-28. Investigation only -- no code, no hooks, no build.
Answers: can controller menu navigation work with a thin slice (0-few
hooks) instead of the full iw3sp_mod binding chain?_

---

## TL;DR -- **YES, menu nav is achievable with ZERO engine hooks.**

It's a pure dispatch refinement inside our own `gp_dispatch_buttons`
(gamepad_buttons.c): when a menu is open (`KEYCATCH_UI` active), remap
the gamepad buttons to the STANDARD menu keys (ENTER / ESCAPE / arrows)
and send them via the existing `Com_QueueEvent` path -- exactly the keys
keyboard menu nav already uses. No engine ABI, no naked stubs, no
keyName table, no Key_SetBinding.

## How the evidence leads there

### 1. CoD4x has NO engine gamepad keynums
`keycodes.h` has `K_JOY1..K_JOY32` / `K_AUX*` only -- **no
K_BUTTON_A / K_DPAD_* / K_APAD_***. So we cannot deliver "engine
gamepad keynums" through CoD4x's Key_Event even if we wanted to.

### 2. Stock MP menus bind to ENGINE gamepad keynums, not K_JOY
`gamemode_choices.menu`, `common_game_options.inc`, `game_options.inc`
use `execKeyInt DPAD_RIGHT / APAD_RIGHT / APAD_LEFT` etc. Those compile
(in ui_mp.ff) to engine keynums 0x14-0x1F. Our Phase 3-B `K_JOY*`
events do NOT match them -> that's why the pad doesn't drive menus today.

### 3. CoD4x doesn't own the UI key handler
No `Menu_HandleKey` / UI key routing in `CoD4x_Client_pub/src/`. The
menu/UI key handling lives entirely in iw3mp.exe. CoD4x just feeds it
keys via `Com_QueueEvent` -> `Key_Event` -> (if `KEYCATCH_UI`) the
engine UI handler.

### 4. iw3sp_mod's menu nav is just "gamepad key -> standard menu key"
`UI_GamepadKeyEvent` (Gamepad.cpp:1555) + `controllerMenuKeyMappings`
(line 158) map:

| gamepad | menu key |
|---|---|
| BUTTON_A / BUTTON_START | **K_ENTER** (select) |
| BUTTON_B / BUTTON_BACK | **K_ESCAPE** (back) |
| DPAD_UP / APAD_UP | **K_UPARROW** |
| DPAD_DOWN / APAD_DOWN | **K_DOWNARROW** |
| DPAD_LEFT / APAD_LEFT | **K_LEFTARROW** |
| DPAD_RIGHT / APAD_RIGHT | **K_RIGHTARROW** |

then calls `Game::UI_KeyEvent(down, localClientNum, mappedKey)`. The
menu navigates on the SAME standard keys keyboard nav uses
(arrows/enter/escape) -- NOT on gamepad-specific keynums. The
"execKeyInt DPAD_*" bindings in the menus are a secondary path; the
primary nav is arrows/enter/escape.

### 5. CoD4x already has everything we need for the standard keys
- `K_UPARROW`, `K_DOWNARROW`, `K_LEFTARROW`, `K_RIGHTARROW`, `K_ENTER`,
  `K_ESCAPE` -- all exist in CoD4x keycodes.h.
- `KEYCATCH_UI = 0x0002` (q_shared.h:1188) -- the "a menu is open" flag.
- `Key_IsCatcherActive(localClientNum, mask)` (client.h:724) -- we
  already call it in gamepad_move.c.
- `Com_QueueEvent(SE_KEY, key, down, ...)` -- our existing delivery.

## The slice (0 engine hooks)

In `gp_dispatch_buttons` (gamepad_buttons.c), branch on menu state:

```
if (Key_IsCatcherActive(0, KEYCATCH_UI)) {
    // menu open: send standard menu keys
    A / Start  -> K_ENTER
    B / Back   -> K_ESCAPE
    DPAD_UP    -> K_UPARROW     (and/or left-stick up)
    DPAD_DOWN  -> K_DOWNARROW
    DPAD_LEFT  -> K_LEFTARROW
    DPAD_RIGHT -> K_RIGHTARROW
} else {
    // in-game: current behavior (K_JOY1..16, bindable)
}
```

This is the SAME mechanism as iw3sp_mod (gamepad -> standard menu key),
just delivered through CoD4x's `Com_QueueEvent` instead of the engine's
`UI_KeyEvent`. No hooks, no naked stubs, no engine memory writes.

### Important: left-stick menu nav may ALREADY work today
At `cl_gamepad_legacy_sticks 1` (the DEFAULT), `Gamepad_ApplyLeftStick`
ALREADY sends `K_UPARROW/DOWN/LEFT/RIGHT` via `Com_QueueEvent`. In a
menu those should already move the highlight. **What's missing is
SELECT (A->ENTER) and BACK (B->ESCAPE)** -- currently A sends K_JOY1
(ignored by the menu) and B sends K_JOY2. So the user can likely move
the cursor but not select/back. The slice adds exactly that.

## Quick validation test (NO code change -- do this first)

On the CURRENTLY DEPLOYED build (legacy=1 default):
1. Launch, `\cl_gamepad 1`, open the main menu.
2. Push the **left stick up/down**. Does the menu highlight move?
   - **YES** -> confirms `Com_QueueEvent` arrow keys navigate menus ->
     the 0-hook slice will work; we only need to add A=ENTER / B=ESCAPE
     / D-pad=arrows gated on KEYCATCH_UI.
   - **NO** -> the engine UI handler isn't consuming our queued arrows
     in menus; then we'd need the engine `UI_KeyEvent` route (still
     small: 1 engine call + Key_IsCatcherActive, no naked stubs).

This single observation decides the implementation precisely.

## Recommendation

- **Menu-nav slice = 0 engine hooks.** Implement the KEYCATCH_UI-gated
  remap in `gp_dispatch_buttons`. Risk is minimal (our own code, no
  engine writes), and it directly solves the user's priority (controller
  works in menus).
- The full iw3sp_mod binding chain (keyName table, GetLocalizedKeyName
  glyphs, Key_SetBinding persistence, Key_GetCommandAssignment) is about
  **binding-config + console-style glyph display**, NOT basic
  navigation. Defer ALL of it -- it's Phase 4 (menu UI) / a later 3-E
  pass, and the heavy `__usercall` + naked-stub + `playerKeys` work from
  the stage3e-port-plan.md is NOT needed for navigation.
- If the quick test shows queued arrows don't navigate, fall back to the
  engine `UI_KeyEvent` route: still only `UI_KeyEvent` VA +
  `Key_IsCatcherActive` (both small, no naked stubs).

## Fallback option (if Com_QueueEvent arrows don't navigate menus)

Route through the engine UI handler like iw3sp_mod:
- Resolve iw3mp `UI_KeyEvent` VA (Ghidra) + its convention.
- When `KEYCATCH_UI`, call `UI_KeyEvent(down, 0, mappedKey)` directly
  with the standard menu keynum.
- Still NO naked stubs, NO keyName table. 1-2 engine calls. ~LOW.

## Scope comparison

| Approach | Hooks | Naked asm | engine-ABI | Solves nav? |
|---|---|---|---|---|
| **0-hook slice (recommended)** | 0 | none | none | yes |
| Engine UI_KeyEvent fallback | 0 install (1 call) | none | UI_KeyEvent VA only | yes |
| Full iw3sp_mod chain (stage3e-port-plan) | 12 | 8 stubs | playerKeys + tables | yes + glyphs + binding-config |

## Decisions for the user

1. Run the **quick validation test** above (left stick in a menu) and
   report -- it picks slice vs fallback with certainty.
2. Confirm the slice scope: **navigation only** (A=select, B=back,
   dpad/stick=move), deferring glyphs + binding-config to Phase 4?
3. Should the slice also handle the **right stick** in menus (e.g. as a
   scroll), or leave right stick view-only / inert in menus?

---
_Status: Phase 3-C complete. Phase 3-E.0 investigation only. No code
written. Awaiting the quick validation test result + decisions above._
