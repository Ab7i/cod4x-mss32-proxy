# Phase 3-E.4 -- Key_SetBinding hooks x3 + persistence -- ANALYSIS

_Written 2026-05-29. Read-only. No code. Pre-flight verified
(DumpKeySetBinding.java)._

---

## 0. TL;DR

- **3 HOOK_JUMP sites** inside iw3mp `Key_SetBinding` (0x552920), all
  clean 5-byte `CALL 0x4678b0`. Same target, three different
  register-setup contexts. Pre-flight matches iw3sp_mod's stub
  patterns exactly:
  - stub01/02 marshal **ECX, EAX** (2 reg args),
  - stub03 marshals **EDX, ECX, EAX** (3 reg args).
- The 3 stubs all call the SAME C helper `gp_key_setbinding_hk` --
  which (per iw3sp_mod) tags `gpad_buttonConfig = "custom"` on gamepad
  keynums + forwards to engine `Game::Key_SetBinding`.
- **Persistence timing fix** (3-E.2's known caveat): move
  `gp_install_keynames` + `gp_install_bindhooks` (+ the new 3-E.4
  install) to BEFORE `config_mp.cfg` exec. Without it, persisted
  `bind BUTTON_A ...` lines lose on next launch.
- **K_JOY -> engine keynum migration** is the parallel concern: until
  `gp_dispatch_buttons` emits engine keynums (BUTTON_A=0x1) instead of
  K_JOY1..16, `bind BUTTON_A` won't trigger from a physical press. I
  recommend keeping migration as a **separate sub-phase (3-E.4b)** so
  the hook port (3-E.4a) is reviewed cleanly first.

## 1. Pre-flight results (DumpKeySetBinding.java)

| Site | iw3mp | Bytes (5) | Insn | Jump-back | Reg setup BEFORE site |
|---|---|---|---|---|---|
| #1 stub01 | `0x5529B8` | `E8 F3 4E F1 FF` | `CALL 0x4678b0` | `0x5529BD` | `MOV ECX,[ESP+0x10]; PUSH 0x6bfea7; MOV EAX,EDI` |
| #2 stub02 | `0x5529CB` | `E8 E0 4E F1 FF` | `CALL 0x4678b0` | `0x5529D0` | `MOV ECX,[ESP+0x18]; MOV EAX,[EBX]; ADD ESP,4; PUSH 0x6bfea7` |
| #3 stub03 | `0x5529E3` | `E8 C8 4E F1 FF` | `CALL 0x4678b0` | `0x5529E8` | `MOV EDX,[EBP+0x10C]; MOV EAX,[EBX]; PUSH EDX; MOV ECX,ESI` |

Key facts:
- **All three are clean 5-byte CALLs** -> the JMP rel32 patch fits
  exactly, no instruction-split worry.
- **Same call target** at every site: `0x4678b0` (small engine helper).
  iw3sp_mod's hooks REPLACE this call with the gamepad-aware Hk in all
  three contexts -- the original `0x4678b0` work is supplanted, not
  resumed (the stubs jmp site+5, past the original call, and the engine
  cleans the pushed string on its own after).
- **Register patterns match iw3sp_mod stubs verbatim** (ECX/EAX x2,
  EDX/ECX/EAX x1). No surprises -> stubs port byte-for-byte modulo the
  jump-back addresses + the C symbol name.

## 2. What the hooks do (iw3sp_mod)

```cpp
void Key_SetBinding_Hk(int localClientNum, int keyNum, const char* binding) {
    if (Key_IsValidGamePadChar(keyNum))                              // engine gamepad keynum range
        Dvars::Functions::SetRaw(Dvars::gpad_buttonConfig, "custom");// mark config as user-modified
    Game::Key_SetBinding(localClientNum, keyNum, binding);           // engine actual binder
}
```
The hooks intercept the engine's internal binding-set call sites so
that whenever a gamepad keynum (BUTTON_A..DPAD_RIGHT) gets rebound,
the `gpad_buttonConfig` dvar flips to `"custom"` (so the menu UI knows
to save a custom layout instead of re-applying the default preset).

iw3sp_mod stubs (Gamepad.cpp:2039-2077) -- the three trampolines our
3-E.4 ports:
```asm
; stub01 (2-reg args)
push ecx
push eax
call Key_SetBinding_Hk
add  esp, 8
jmp  jump_offset            ; iw3sp 0x56F72D -> iw3mp 0x5529BD

; stub02 -- identical shape, different jump_offset
;            iw3sp 0x56F740 -> iw3mp 0x5529D0

; stub03 (3-reg args)
push edx
push ecx
push eax
call Key_SetBinding_Hk
add  esp, 0xC
jmp  jump_offset            ; iw3sp 0x56F758 -> iw3mp 0x5529E8
```

Both stub01 and stub02 push (ECX, EAX) -> `Hk(int=EAX, int=ECX, ...)`
where the original engine PUSH-before-CALL leaves a third arg on the
stack at [esp+0xC]. stub03 explicitly pushes a third register (EDX) and
cleans 0xC. The Hk signature unifies as `(int lc, int keyNum, const
char* binding)` -- the third arg position varies by stub but the cdecl
contract holds.

(Per-stub iw3mp-specific marshalling will be confirmed at implementation
time -- the pre-flight shows the register setup matches, but the exact
arg-order mapping (which reg = lc vs keyNum) needs a one-shot trace at
runtime with a diagnostic Printf before turning the hook live.)

## 3. Engine-ABI needs

| Item | Status |
|---|---|
| iw3mp playerKeys (0x8F1CA0 + qkey_t.binding) | already exposed by CoD4x `keys.h` (used in 3-E.3) |
| `Game::Key_SetBinding` iw3mp VA -- the FINAL writer the Hk forwards to | **TODO**: resolve via Ghidra. iw3sp_mod calls `Game::Key_SetBinding(...)` (an external engine fn). CoD4x may already have it as a re-impl in cl_keys.c -- worth grep before adding a raw VA. |
| `gpad_buttonConfig` dvar | not yet registered in CoD4x. Add as `Cvar_RegisterString("gpad_buttonConfig", "default", CVAR_ARCHIVE, ...)` at init -- cheap. |
| `Key_IsValidGamePadChar` | done (3-E.3, gamepad_keys.c) -- engine keynum ranges. |

No new playerKeys / clientActive struct reverse-engineering. The
heaviest unknown is `Game::Key_SetBinding`'s iw3mp VA + convention.

## 4. Persistence timing fix (the 3-E.2 caveat)

**Problem (recap):** `gp_install_keynames` + `gp_install_bindhooks` are
called from `IN_StartupGamepads`, which runs DURING client init AFTER
`config_mp.cfg` is exec'd. So a persisted `bind BUTTON_A "+attack"`
line is parsed by the engine before our table patch + before the Hk
hooks are installed -> the bind drops on first launch and the user
loses their custom layout.

**Fix (3-E.4 task):** move our installs to BEFORE the config exec.
Candidate insertion points in CoD4x (to investigate):
- `Com_Init` in common.c -- right after Cmd/Cvar init, before
  `Cbuf_ExecuteText(EXEC_NOW, "exec ...")` style calls.
- An early CL_Init step.
- (Cleanest) a tiny `gp_init_early()` wrapper called from common.c at
  the right line, mirroring how 3-E.1 wrapped Com_EventLoop's SE_KEY
  dispatch.

Implementation note: keep `gp_install_keynames`/`gp_install_bindhooks`
idempotent (already done via static guards), so calling them earlier
AND from `IN_StartupGamepads` (e.g. on in_restart) is harmless.

## 5. K_JOY -> engine keynum migration (3-E.4b, separate)

The Path A tension flagged in 3-E.3 persists: even with 3-E.4 done,
`bind BUTTON_A "+attack"` resolves to engine keynum 0x1, but our
`gp_dispatch_buttons` emits K_JOY1 (~207) for the physical A press, so
the binding doesn't fire from the controller. To close that loop, the
input emission must switch from K_JOY* to the engine BUTTON_*/DPAD_*
keynums.

**Recommendation:** do this as **3-E.4b** AFTER the Key_SetBinding hooks
port (3-E.4a) is verified, NOT bundled with it. Two reasons:
1. 3-E.4a is a self-contained, low-risk change (stubs + Hk + early
   install) that can be tested independently.
2. The migration breaks Path A compatibility -- existing
   `bind JOY1 "+attack"` lines in user configs become inert overnight.
   Need a `cl_gamepad_legacy_input` cvar (default 1 for first ship?)
   to A/B test before flipping the default, the same pattern as
   `cl_gamepad_legacy_sticks` in Phase 3-C.

Implementation outline for 3-E.4b (NOT this session):
- Add `s_gp_ui_button_list[]` shape used in 3-E.0's slice analysis,
  but emitting engine BUTTON_* keynums when the migration cvar is 0.
- Reuse the GAME_K_* constants from gamepad_internal.h.
- Stage 3A stick-as-arrow code already emits standard keys -- only the
  button dispatch flips.
- Test: with migration on, `bind BUTTON_A "+attack"` from the menu UI
  triggers shooting on A press.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Wrong arg-order in the Hk (which reg = lc vs keyNum vs binding) | One-shot Printf at Hk entry logging the 3 values; verify against a known `bind` operation, THEN remove. iw3sp_mod's signature is `(lc, keyNum, binding)` but the SP/MP register mapping may differ. |
| Forwarding to `Game::Key_SetBinding` with the wrong VA / convention | Same convention pre-flight discipline as 3-C / 3-E.3 -- disassemble before calling. |
| Persistence install runs BEFORE Cvar/Cmd subsystems are ready | Validate the insertion point in Com_Init carefully; keep guard variables. |
| Hook on Key_SetBinding interacts with CoD4x's reimplemented Key_SetBinding (if it exists) | grep first; if CoD4x reimplements, the engine hook may be partially superseded (3-E.1-style finding); plan B = wrap CoD4x's version too. |
| K_JOY/BUTTON_* dual-emit during 3-E.4b transition | `cl_gamepad_legacy_input` toggle + migration plan documented. |

## 7. Estimate
- **3-E.4a** (3 stubs + Hk + Game::Key_SetBinding resolve + early
  install): **1 session** (toolchain ready from 3-E.3; the stubs are 1:1
  ports).
- **3-E.4b** (K_JOY -> engine keynum migration + legacy cvar): **0.5-1
  session** (mainly gp_dispatch_buttons + a cvar + smoke test).

**Total ~1.5-2 sessions** for the binding chain finish.

## 8. Decisions for the user
1. Split 3-E.4 into **3-E.4a (hooks)** and **3-E.4b (input migration)**,
   or do them together? (Recommend split.)
2. **Game::Key_SetBinding** -- check first whether CoD4x reimplements it
   (if yes, edit CoD4x; if no, find iw3mp VA + convention)?
3. **Early install site:** prefer `Com_Init` (common.c, before config
   exec) or hook a CoD4x init function? (Recommend common.c edit, same
   pattern as 3-E.1.)
4. **Migration cvar name:** `cl_gamepad_legacy_input` (matches
   `cl_gamepad_legacy_sticks`) -- agreed?

---
_Status: Phase 3-E.3 verified + committed (e2d6978). Phase 3-E.4
analysis only -- no code. Pre-flight confirms 3 clean 5-byte CALL sites
with iw3sp_mod-matching register setup; stubs port cleanly. Two main
work items: the 3 hooks (3-E.4a) and the K_JOY->engine migration
(3-E.4b). Awaiting decisions in section 8._
