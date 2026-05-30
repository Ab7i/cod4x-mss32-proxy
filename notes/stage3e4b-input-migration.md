# Phase 3-E.4b -- K_JOY -> engine keynum migration -- ANALYSIS

_Written 2026-05-29. Read-only. No code. Resolves the Path A tension
from 3-E.3 / 3-E.4a (`bind BUTTON_A` accepted but never fires from a
physical A press because we emit K_JOY1)._

---

## 0. TL;DR

Replace the keynum constants in `gp_dispatch_buttons` so a gamepad
press emits the **engine** keynum (BUTTON_A=0x1 ... DPAD_RIGHT=0x17)
instead of the CoD4x K_JOY1..16. Gate behind a new
**`cl_gamepad_legacy_input`** cvar (default 1 = Phase 3-B/Path A; 0 =
engine keynums) so existing `bind JOY1 ...` user configs keep working.

This is the moment the whole 3-E chain "wakes up":
- 3-E.2 keyName table -> `bind BUTTON_A` parses.
- 3-E.3 Key_GetCommandAssignment -> engine HUD hints find the bind.
- 3-E.4a Key_SetBinding hooks -> `gpad_buttonConfig`="custom" on bind.
- **3-E.4b dispatch -> the physical press finally reaches the binding.**

It's also the moment the **engine UI handler** receives gamepad
keynums in menus, potentially unblocking the menu-navigation problem
from 3-E.0 (KEYCATCH_UI + BUTTON_A/DPAD_* may now drive nav).

## 1. Current state (gp_dispatch_buttons)

`gamepad_buttons.c` -- a single table + a single loop:

```c
static const gp_button_to_code_t s_gp_button_list[] = {
    { GPAD_A,        K_JOY1  }, { GPAD_B,        K_JOY2  },
    { GPAD_X,        K_JOY3  }, { GPAD_Y,        K_JOY4  },
    { GPAD_L_SHLDR,  K_JOY5  }, { GPAD_R_SHLDR,  K_JOY6  },
    { GPAD_BACK,     K_JOY7  }, { GPAD_START,    K_JOY8  },
    { GPAD_L3,       K_JOY9  }, { GPAD_R3,       K_JOY10 },
    { GPAD_UP,       K_JOY11 }, { GPAD_DOWN,     K_JOY12 },
    { GPAD_LEFT,     K_JOY13 }, { GPAD_RIGHT,    K_JOY14 },
    { GPAD_L_TRIG,   K_JOY15 }, { GPAD_R_TRIG,   K_JOY16 },
};
void gp_dispatch_buttons(int port) {
    /* for each entry: edge-detect + Com_QueueEvent(SE_KEY, code, ...) */
}
```
`gp_release_all` walks the same table.

## 2. The migration mapping (engine keynums, confirmed)

Source: iw3sp_mod `buttonList` (Gamepad.cpp:6-24) + gamepad_internal.h
GAME_K_* constants (already defined since 3-A).

| XInput button   | Phase 3-B (K_JOY) | Phase 3-E.4b (engine, GAME_K_*) | hex  |
|---|---|---|---|
| A               | K_JOY1            | `GAME_K_BUTTON_A`               | 0x01 |
| B               | K_JOY2            | `GAME_K_BUTTON_B`               | 0x02 |
| X               | K_JOY3            | `GAME_K_BUTTON_X`               | 0x03 |
| Y               | K_JOY4            | `GAME_K_BUTTON_Y`               | 0x04 |
| LB (L_SHLDR)    | K_JOY5            | `GAME_K_BUTTON_LSHLDR`          | 0x05 |
| RB (R_SHLDR)    | K_JOY6            | `GAME_K_BUTTON_RSHLDR`          | 0x06 |
| Back            | K_JOY7            | `GAME_K_BUTTON_BACK`            | 0x0F |
| Start           | K_JOY8            | `GAME_K_BUTTON_START`           | 0x0E |
| L3 (L_STICK)    | K_JOY9            | `GAME_K_BUTTON_LSTICK`          | 0x10 |
| R3 (R_STICK)    | K_JOY10           | `GAME_K_BUTTON_RSTICK`          | 0x11 |
| DPAD_UP         | K_JOY11           | `GAME_K_DPAD_UP`                | 0x14 |
| DPAD_DOWN       | K_JOY12           | `GAME_K_DPAD_DOWN`              | 0x15 |
| DPAD_LEFT       | K_JOY13           | `GAME_K_DPAD_LEFT`              | 0x16 |
| DPAD_RIGHT      | K_JOY14           | `GAME_K_DPAD_RIGHT`             | 0x17 |
| LT (L_TRIG)     | K_JOY15           | `GAME_K_BUTTON_LTRIG`           | 0x12 |
| RT (R_TRIG)     | K_JOY16           | `GAME_K_BUTTON_RTRIG`           | 0x13 |

All 16 keynums covered; all are inside the gamepad ranges
`gp_key_is_valid_gamepad_char` accepts (0x1-0x6, 0xE-0x17). 100%
compat with the 3-E.2 keyName table + 3-E.3/3-E.4a hooks.

## 3. Cvar design -- `cl_gamepad_legacy_input`

| field | value |
|---|---|
| name | `cl_gamepad_legacy_input` |
| type | Bool |
| default | **1** (Phase 3-B behavior, K_JOY1..16) |
| flags | `CVAR_ARCHIVE` |
| help text | "Use legacy K_JOY1..16 keynums for gamepad buttons (compat with existing `bind JOY1 ...` configs). Set 0 to emit engine BUTTON_*/DPAD_* keynums for `bind BUTTON_A`-style configs and proper menu navigation." |

Naming mirrors `cl_gamepad_legacy_sticks` (Phase 3-C). Same pattern:
default = safe/legacy, user opts in to the new path. Default flips to
0 in a future phase after broad testing + a config-migration helper
(see section 7).

## 4. Dispatcher changes (proposed, NOT applied)

Smallest possible footprint -- add a second table + a runtime selector:

```c
/* gamepad_buttons.c additions */
static const gp_button_to_code_t s_gp_button_list_engine[] = {
    { GPAD_A,        GAME_K_BUTTON_A      },
    { GPAD_B,        GAME_K_BUTTON_B      },
    ... (16 entries as in the table above) ...
};

void gp_dispatch_buttons(int port) {
    const gp_button_to_code_t *list;
    int count;

    if (port < 0 || port >= GP_MAX_GPAD_COUNT) return;
    if (!gp_state[port].enabled) return;

    /* Pick table per cvar. legacy_input is non-static extern declared
     * in gamepad_internal.h; registered in IN_StartupGamepads. */
    if (cl_gamepad_legacy_input && !cl_gamepad_legacy_input->boolean) {
        list = s_gp_button_list_engine;
        count = sizeof(s_gp_button_list_engine) / sizeof(s_gp_button_list_engine[0]);
    } else {
        list = s_gp_button_list;
        count = GP_BUTTON_LIST_COUNT;
    }
    /* ...same edge loop, walking `list[0..count]`... */
}
```

`gp_release_all` needs the same selector (release whichever table was
active). A subtle interaction: if the user toggles the cvar **while a
button is held**, the press came from one table and the release
target switches mid-hold -> stuck-down keynum. Mitigation:

- Cheap: on cvar change, force-release the previously-active table
  (a single call to gp_release_all in the old mode + reset
  lastDigitals). Watch via a static prev-cvar-value flag in
  gp_dispatch_buttons.
- Acceptable: ignore (mid-hold toggling is rare; one stuck press at
  worst, cleared by next press/release cycle).

Recommend the cheap mitigation (~5 lines).

## 5. Compat strategy

Today's users have `bind JOY1 "+attack"` etc. in `config_mp.cfg`
from Phase 3-B. Three states post-3-E.4b:

| user state | what happens at default `cl_gamepad_legacy_input=1` | what happens after they `\cl_gamepad_legacy_input 0` |
|---|---|---|
| Existing user (JOY* binds) | works (Phase 3-B identical) | JOY* binds inert; needs to re-bind as BUTTON_* |
| New user (no binds yet) | nothing bound on gamepad | nothing bound (engine has no default BUTTON_* binds) |
| Power user (both) | JOY* binds active; BUTTON_* binds dormant (engine sees BUTTON_* but we emit JOY*) | BUTTON_* binds active; JOY* inert |

No silent breakage at default. The opt-in is explicit and documented
in the help text. Auto-migration (rewrite JOY* -> BUTTON_* in config)
is **3-E.5 territory** -- it needs a one-shot config rewriter run on
upgrade, gated by a version stamp dvar. Out of scope here.

## 6. Testing plan

| Test | Steps | Expect |
|---|---|---|
| Regression (legacy=1) | game launch, `bind JOY1 "say hello"`, press A | "hello" in chat (identical to Phase 3-B) |
| Migration toggle | `\cl_gamepad_legacy_input 0`, press A | JOY1 binding inert (no "hello") |
| Engine bind activates | `\bind BUTTON_A "say world"`, press A | "world" in chat (FIRST time a `bind BUTTON_A` actually fires from a press) |
| Console gating | open console, press A | nothing (KEYCATCH_CONSOLE -> our dispatch suppresses) |
| `gpad_buttonConfig` | `\bind BUTTON_A "+attack"` then `\gpad_buttonConfig` | "custom" (3-E.4a path still working) |
| Menu navigation (the big one) | open main menu, press D-PAD_UP / A / B | UP highlights / A selects / B backs -- IF the engine UI handler recognizes DPAD/BUTTON keynums |
| Diagnostic | `\cl_gamepad_debug 1`, press a button, check console | (no current spam expected -- the existing 3-E.1 one-shot kb-vs-gamepad log fires once) |
| Stuck-key on toggle | hold A, toggle cvar, release A | no "stuck down" (force-release mitigation) |

The **menu navigation test** is the headline -- it answers the
3-E.0 open question. If 3-E.4b alone makes menus respond to the pad,
we have working navigation without needing 3-E.6 (glyph stubs) or
extra UI hooks. If not, 3-E.6 / UI_KeyEvent fallback comes next.

## 7. Future (NOT this phase)

- **3-E.5**: persistence + config migration. The 3-E.2 timing caveat
  (gp_install_keynames runs AFTER config exec -> persisted BUTTON_*
  binds dropped on first launch) + an optional one-shot rewriter that
  converts `bind JOY1 X` -> `bind BUTTON_A X` in config_mp.cfg, gated
  by a `cl_gamepad_migrated` stamp. Lets us eventually default
  `cl_gamepad_legacy_input` to 0 without breaking anyone.
- **3-E.6**: GetLocalizedKeyName naked stubs (HIGH complexity, flag-
  sensitive). Adds glyph rendering. Independent of input migration.
- **Phase 4**: the menu UI page (Gamepad settings + presets + the
  `cl_gamepad_aimassist` toggle wired but inert until Phase 3-D).

## 8. Risks

| Risk | Mitigation |
|---|---|
| Stuck button across cvar toggle | force-release prev-table mid-toggle (~5 lines) |
| Engine UI handler ignores BUTTON_* keynums | confirmed it DOES (iw3sp_mod proves it; main MP menus have `execKeyInt DPAD_*` -- now reachable). If specific menus still don't bind, glyph paths via 3-E.6 |
| Mismatch between keynums emitted and Key_GetCommandAssignment scan | NONE -- both use the same engine ranges (0x1-0x6, 0xE-0x17). Designed to match. |
| Some engine code reads K_JOY* directly (unlikely but possible) | grep iw3mp before flipping defaults; for now legacy=1 keeps both paths viable |
| User confusion ("my JOY1 binds stopped working") | clear help text + leave default=1; future 3-E.5 migration smooths the path |

## 9. Estimate
**1 session.** ~30 lines of code (second table + cvar registration +
selector branch + force-release mitigation), no asm, no engine-ABI.
Heaviest part is the smoke test matrix in section 6 (including the
menu-nav verification).

## 10. Decisions for the user
1. cvar name `cl_gamepad_legacy_input` -- agreed (matches
   `cl_gamepad_legacy_sticks`)?
2. Stuck-key mitigation: implement the cheap force-release across the
   cvar toggle, or skip it for now?
3. Where to register the cvar: alongside `cl_gamepad_legacy_sticks`
   in `IN_StartupGamepads` (gamepad.c)?
4. After 3-E.4b ships, do we go straight to the **menu-nav
   verification** (key payoff) before 3-E.5/3-E.6, to learn if the
   migration alone solves the original menu problem?

---
_Status: Phase 3-E.4a verified + committed. Phase 3-E.4b analysis
only -- no code. Mapping table verified against iw3sp_mod +
gamepad_internal.h. Awaiting decisions in section 10._
