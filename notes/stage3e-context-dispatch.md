# Phase 3-E — context-aware dispatch (menu nav) -- ANALYSIS

_Written 2026-05-28 after the user's in-menu test FAILED (no nav, no
select, no back, left stick doesn't move the cursor; in-game fine, no
crash). Read-only analysis. No code, no build._

---

## 0. The test result rewrites the diagnosis

My earlier slice plan assumed CoD4 MP menus navigate via arrow keys
(K_UPARROW...) + ENTER, like iw3sp_mod's SP menus. **That assumption is
WRONG.** The investigation shows:

### Finding 1 — CoD4 MP menus are MOUSE-DRIVEN (hover + click)
`common_macro.inc` button macros use **`onFocus { }`** (mouse-hover
focus). A grep across ALL `ui_mp/*.menu` + `*.inc` for arrow/enter
navigation bindings (`execKey UPARROW|DOWNARROW|ENTER`) returns
**nothing**. The only gamepad-ish `execKeyInt DPAD_*/APAD_*` lines are
in a few list widgets (gamemode_choices) for left/right value cycling,
not general navigation.

=> The main menu has **no arrow-key or ENTER navigation handlers at
all**. A keyboard's arrow keys don't navigate it either. So our queued
`K_UPARROW` (from the left stick) does nothing because **nothing in the
menu listens for it** -- it is NOT that gamepad events are "filtered".

### Finding 2 — navigation = mouse cursor; selection = mouse click
Buttons highlight on `onFocus` (cursor hover) and activate on click
(K_MOUSE1). This is the actual CoD4 MP menu model.

### Finding 3 — iw3sp_mod's K_ENTER/arrow mapping is SP-specific
`UI_GamepadKeyEvent` + `controllerMenuKeyMappings` map gamepad ->
ENTER/UPARROW/ESCAPE and call `Game::UI_KeyEvent` (iw3sp 0x567C80,
__usercall: `down` in EDI). That works for **SP** menus, which DO
support keyboard nav. **MP menus don't** -- so copying that mapping
verbatim would still do nothing in MP. We must adapt to the mouse model.

## 1. Why left-stick K_UPARROW does nothing (task 1 answer)

`Gamepad_ApplyLeftStick` (gamepad.c) sends `K_UPARROW` via
`Com_QueueEvent(g_wv.sysMsgTime, SE_KEY, K_UPARROW, down, 0, NULL)` --
the SAME API the rest of CoD4x input uses; the event DOES reach the
engine (proven in-game: it moves the player). It "does nothing" in a
menu simply because **the MP menu has no K_UPARROW handler** -- there is
no cursor-by-arrows in these menus. Not an API problem, not filtering:
the menu just doesn't bind that key.

(At `legacy=1` default the left stick still emits arrows; in a menu
they're inert. That's expected and harmless.)

## 2. The CORRECT slice = mouse model (still ~0 engine hooks)

For mouse-driven MP menus the controller should:

| Input | In-menu action | Mechanism |
|---|---|---|
| **Right stick** | move the cursor | already -> CL_MouseEvent (legacy=1). **UNCONFIRMED in menus -- must test.** |
| **A** | click hovered item | `Com_QueueEvent(SE_KEY, K_MOUSE1, down/up)` |
| **B** | back / close | `Com_QueueEvent(SE_KEY, K_ESCAPE, down/up)` |
| **Left stick / D-pad** | (optional) also move cursor | could feed CL_MouseEvent like the right stick, or stay inert |

K_MOUSE1 (200), K_ESCAPE -- both exist in CoD4x keycodes.h. KEYCATCH_UI
= 0x0002 (q_shared.h). `Key_IsCatcherActive(localClientNum, mask)`
exists (client.h:724; impl cl_keys.c:240) -- already used in
gamepad_move.c, just needs `#include "client.h"` in gamepad_buttons.c.

**This is still a 0-engine-hook slice** -- a context branch in our own
`gp_dispatch_buttons`, using existing keynums + the existing CL_MouseEvent
right-stick path.

### The ONE decisive unknown -> a 60-second user test
**Does the RIGHT stick move the mouse cursor in a menu right now?**
(legacy=1 build already feeds CL_MouseEvent every frame.) The user tested
the LEFT stick (arrows -> inert). They did NOT report the RIGHT stick.
- **If right stick moves the menu cursor** -> the slice is trivial:
  add A=K_MOUSE1, B=K_ESCAPE gated on KEYCATCH_UI. Cursor already works.
- **If it does NOT** -> CL_MouseEvent deltas feed the in-game view, not
  the menu cursor; then we need to drive the menu cursor position
  another way (engine cursor var, or UI_KeyEvent path). Bigger.

## 3. How Phase 3-B's gp_dispatch_buttons works today

```
gp_dispatch_buttons(port):
  for each button in s_gp_button_list[]:   // GPAD_* -> K_JOY1..16
    if pressed  -> Com_QueueEvent(SE_KEY, K_JOY*, qtrue)
    if released -> Com_QueueEvent(SE_KEY, K_JOY*, qfalse)
```
No awareness of menu vs game. Always K_JOY*. In a menu the K_JOY* events
are inert (no menu handler), which is exactly the symptom.

## 4. Proposed design (context-aware dispatch)

Add a SECOND mapping table for the UI context and branch on KEYCATCH_UI.
**Note the UI table is MOUSE-oriented (A=K_MOUSE1, B=K_ESCAPE), NOT the
ENTER/arrow table from the earlier (wrong) plan.**

```c
// in gamepad_buttons.c
#include "client.h"   // Key_IsCatcherActive

// In-menu (mouse model). Only the buttons that map to a menu action.
static const gp_button_to_code_t s_gp_ui_button_list[] = {
    { GPAD_A,      K_MOUSE1  },   // click hovered item
    { GPAD_B,      K_ESCAPE  },   // back / close
    { GPAD_BACK,   K_ESCAPE  },
    // optional: Start -> K_ESCAPE or a menu toggle
    // D-pad: left inert for now (cursor comes from the right stick)
};

void gp_dispatch_buttons(int port) {
    qboolean ui = Key_IsCatcherActive(0, KEYCATCH_UI);
    const gp_button_to_code_t *table = ui ? s_gp_ui_button_list : s_gp_button_list;
    int count = ui ? GP_UI_BUTTON_LIST_COUNT : GP_BUTTON_LIST_COUNT;
    // ... same pressed/released edge loop, but over `table`/`count`
}
```

Right-stick cursor stays as-is (IN_GamepadsMove -> Gamepad_ApplyRightStick
-> CL_MouseEvent, unconditional). Optionally also route the LEFT stick to
CL_MouseEvent when `ui` is true (so either stick moves the cursor) --
small addition in IN_GamepadsMove.

### Edge cases / risks
- **Edge-detect across context switch:** if A is held while a menu opens/
  closes, the press table changes mid-hold and the matching release may
  be sent on the other table (e.g. press K_JOY1 in-game, release as
  K_MOUSE1 in-menu) -> a stuck key. Mitigation: on KEYCATCH_UI
  transition, force-release the previous table's held keys (we already
  have gp_release_all; add a context-change release).
- **K_MOUSE1 via Com_QueueEvent actually clicking menu items:** the real
  mouse uses the same SE_KEY/K_MOUSE1 path, so a queued K_MOUSE1 at the
  current cursor position should register as a click. UNCONFIRMED ->
  part of the test.
- **B=K_ESCAPE closing too much:** ESCAPE may close the whole menu stack
  vs one level. Acceptable for a first slice; refine later.
- **In-game unaffected:** the `ui` branch only triggers under
  KEYCATCH_UI, so in-game bindings (K_JOY*) are untouched.
- **Right stick double-duty:** it drives the view in-game and (hopefully)
  the cursor in-menu via the same CL_MouseEvent -- no change needed, but
  sensitivity may feel different in menus (cosmetic).

## 5. Fallback if the mouse model fails the test

If queued K_MOUSE1 doesn't click and/or the right stick doesn't move the
menu cursor, port iw3sp_mod's engine path minimally:
- `Game::UI_KeyEvent` (iw3sp 0x567C80, __usercall `down` in EDI) -> find
  the iw3mp twin (Ghidra), call it directly when KEYCATCH_UI with the
  mapped key. iw3sp_mod calls it via a tiny naked asm (EDI marshalling).
- Still far smaller than the full binding chain; ~1 engine fn + a 3-line
  asm shim. Only if the 0-hook mouse model fails.

## 6. Recommendation

1. **User test first (no code):** in the current build, open a menu and
   move the **RIGHT stick** -- does the cursor move? Also try the
   right-stick cursor over a button + note if hovering highlights it.
2. If cursor moves: implement the **0-hook mouse-model slice** (A=K_MOUSE1,
   B=K_ESCAPE, KEYCATCH_UI-gated, + context-change release). ~**1
   session.**
3. If cursor doesn't move: escalate to the UI_KeyEvent fallback (resolve
   the iw3mp twin first). ~1.5 sessions.

Either way: **navigation only**, glyphs + binding-config stay deferred to
Phase 4 / later 3-E.

## 7. Time estimate
- Mouse-model slice (expected path): **1 session** (dispatch branch +
  context-change release + test).
- UI_KeyEvent fallback (if needed): +0.5-1 session (Ghidra twin + asm shim).

## 8. Decisions for the user
1. **Run the right-stick-in-menu test** and report (cursor moves? hover
   highlights? — this picks the path).
2. Confirm the mouse-model mapping (A=click, B=back). Want Start mapped
   (e.g. Start=ESCAPE/toggle)? Want LEFT stick to also move the cursor
   in menus?
3. Accept "navigation only" scope (no glyphs/binding-config now)?

---
_Status: Phase 3-C complete. Phase 3-E.0 analysis only -- no code. The
earlier arrow/ENTER assumption is RETRACTED; CoD4 MP menus are
mouse-driven. Awaiting the right-stick-in-menu test + decisions._
