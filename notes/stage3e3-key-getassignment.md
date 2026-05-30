# Phase 3-E.3 -- Key_GetCommandAssignmentInternal hook -- ANALYSIS

_Written 2026-05-28. Read-only. No implementation code. Disasm-verified
(DumpKeyGetAssign.java)._

---

## 0. TL;DR

- The hook makes the engine's "which key is bound to command X" lookup
  **gamepad-aware**: when the pad is in use it remaps a few commands
  (+activate/+reload -> +usereload) and returns ONLY gamepad keys; when
  not, returns only non-gamepad keys. Drives the on-screen "[BUTTON] Use"
  hints + the engine's internal binding resolution.
- iw3mp target **0x4678E0** (function ENTRY). **__usercall**: arg1
  (localClientNum) in EAX, arg2 (command) + arg3 (keys[2]) on the stack.
  Needs the **first naked-asm trampoline** -- but the SIMPLEST one (pure
  arg marshalling + `ret`, NO embedded iw3sp addresses to translate,
  because it fully re-implements the function and never resumes the
  original body).
- **engine-ABI is already available -- the deferred-to-3-D concern does
  NOT apply.** CoD4x's `keys.h` exposes the engine playerKeys via
  `#define playerKeys (*((PlayerKeyState_t*)(0x8F1CA0)))` and the
  `qkey_t { qboolean down; int repeats; const char* binding; }` layout.
  Our reimplementation reads `playerKeys.keys[k].binding` directly --
  no new struct reverse-engineering.
- **Conflict (handled by design):** CoD4x reimplements its OWN
  `Key_GetCommandAssignment` (cl_keys.c, used by CL_GetKeyBinding) -- a
  SEPARATE path. The 3 engine callers of 0x4678E0 (verified below) are
  what we hook; we don't touch CoD4x's version.

## 1. What the hook does (iw3sp_mod Gamepad.cpp:1003)

```cpp
int Key_GetCommandAssignmentInternal(int localClientNum, const char* cmd, int(*keys)[2]) {
    int keyCount = 0;
    (*keys)[0] = -1; (*keys)[1] = -1;
    if (gamePads[0].inUse) {
        const char* gpCmd = GetGamePadCommand(cmd);   // +activate/+reload -> +usereload
        for (int k = 0; k < K_LAST_KEY; k++) {         // K_LAST_KEY = 0xDF
            if (!Key_IsValidGamePadChar(k)) continue;  // gamepad keynums only
            if (playerKeys[0].keys[k].binding && strcmp(playerKeys[0].keys[k].binding, gpCmd) == 0)
                if ((*keys)[keyCount++] = k, keyCount >= 2) return keyCount;
        }
    } else {
        for (int k = 0; k < K_LAST_KEY; k++) {
            if (Key_IsValidGamePadChar(k)) continue;   // NON-gamepad keynums only
            if (playerKeys[0].keys[k].binding && strcmp(playerKeys[0].keys[k].binding, cmd) == 0)
                if ((*keys)[keyCount++] = k, keyCount >= 2) return keyCount;
        }
    }
    return keyCount;
}
```

Helpers (both trivial, portable C):
```cpp
const char* GetGamePadCommand(const char* c) {
    if (!strcmp(c,"+activate") || !strcmp(c,"+reload")) return "+usereload";
    if (!strcmp(c,"+melee_breath"))                     return "+holdbreath";
    return c;
}
bool Key_IsValidGamePadChar(int k) {  // ranges from keynum_t
    return (k>=1 && k<=6) || (k>=0xE && k<=0x17) || (k>=0x1C && k<=0x1F);
}   // == GAME_K_FIRST/LAST_GAMEPADBUTTON_RANGE_1/2/3 (already in gamepad_internal.h)
```

iw3sp_mod's naked stub (Gamepad.cpp:1056) -- marshals EAX + 2 stack
args, calls the C++ impl, stores the result, `ret`:
```asm
push eax
pushad
push [esp+0x20+0x4+0x8]   ; arg3 keys
push [esp+0x20+0x4+0x8]   ; arg2 command
push eax                  ; arg1 localClientNum (was in EAX)
call Key_GetCommandAssignmentInternal
add  esp, 0xC
mov  [esp+0x20], eax       ; overwrite saved-eax slot with return
popad
pop  eax
ret
```
**No embedded iw3sp addresses** -- it's a self-contained entry
replacement. Ports to NASM 1:1 (just the C symbol name changes).

## 2. iw3mp target + convention (DumpKeyGetAssign.java)

- Address **0x4678E0** (from address map, `accepted`). FUNCTION ENTRY.
- First bytes: `53 69 C0 28 0D 00 00 55` =
  `PUSH EBX` (1) ; `IMUL EAX,EAX,0xd28` (6) ; `PUSH EBP` (1).
- `IMUL EAX,EAX,0xd28` reads **EAX before any write** => arg1 in EAX
  (localClientNum, scaled by client-struct size 0xd28). **__usercall**,
  confirmed.
- A 5-byte `JMP rel32` overwrites `PUSH EBX` + 4 bytes of the IMUL
  (splits it). **Safe** because the stub re-implements the whole
  function and ends with `ret` -- the original body (and the split
  IMUL) is never executed. (This is exactly why iw3sp_mod's stub `ret`s
  instead of jumping back.)
- **3 engine callers** of 0x4678E0: 0x46857B (fn 0x468560),
  0x55299B (fn 0x552920 = Key_SetBinding), 0x441F03 (fn 0x441C60).
  These are the engine's internal binding/hint lookups -> hooking
  0x4678E0 makes all three gamepad-aware. CoD4x's own
  Key_GetCommandAssignment (cl_keys.c) is NOT among them (separate path).

## 3. engine-ABI: ALREADY EXPOSED by CoD4x (no 3-D dependency)

- Engine indexes playerKeys at `IMUL EAX,EAX,0xd28 ; ADD EAX,0x8f1dcc`
  (base 0x8f1dcc region, stride 0xd28 per client).
- CoD4x `keys.h`:
  ```c
  #define playerKeys (*((PlayerKeyState_t*)(0x8F1CA0)))
  typedef struct { qboolean down; int repeats; const char* binding; } qkey_t;
  typedef struct PlayerKeyState_s { ...; qkey_t keys[MAX_KEYS]; ... } PlayerKeyState_t;
  ```
  => CoD4x already maps the engine playerKeys (0x8F1CA0) with the
  `qkey_t.binding` field we need. Our reimpl reads
  `playerKeys.keys[k].binding` via the existing macro -- **no new
  reverse-engineering, no 3-D engine-ABI work.** (For MAX_LOCAL_CLIENTS=1
  the localClientNum is always 0, so the single CoD4x `playerKeys`
  instance suffices; we can ignore the EAX scaling and just use index 0.)
- This RESOLVES the earlier "defer playerKeys to Stage 3-D" note: that
  was about reverse-engineering the struct; CoD4x already did it.

## 4. Conflict map (who reads what)

| consumer | owner | reads | we hook? |
|---|---|---|---|
| engine binding/hint lookup (3 callers of 0x4678E0) | engine | engine playerKeys 0x8F1CA0 | **YES (0x4678E0)** |
| CoD4x CL_GetKeyBinding -> Key_GetCommandAssignment | CoD4x cl_keys.c | same playerKeys 0x8F1CA0 | no (separate; leave as-is) |

Both ultimately read the same playerKeys memory, but via different
functions. We only need the engine path gamepad-aware for the HUD/menu
hints; CoD4x's CL_GetKeyBinding path is unaffected and fine.

## 5. Implementation plan (NOT applied)

New files / changes:
1. **`src/gamepad_stubs.asm` (NEW NASM)** -- the first naked trampoline:
   `gp_key_getcmdassign_stub` (1:1 port of iw3sp_mod's stub, our C symbol
   name). Wire into CMakeLists `ASM_NASM` sources next to
   client_callbacks.asm. (`--prefix _`, label without underscore.)
2. **`src/gamepad_keys.c`** (extend): add
   - `gp_get_gamepad_command(const char*)` (the +usereload/+holdbreath remap),
   - `gp_key_is_valid_gamepad_char(int)` (the 3 range checks),
   - `int __cdecl gp_key_getcmdassign(int lc, const char* cmd, int (*keys)[2])`
     -- the reimpl reading `playerKeys.keys[k].binding` (keys.h) +
     `gp_state[0].inUse`. cdecl (the stub pushes all 3 args as stack args).
3. **Install:** `GP_HOOK_JUMP(0x4678E0, gp_key_getcmdassign_stub)` in
   gp_install_keynames (or a new gp_install_bindhooks). Self-unprotecting
   via Patch_SetJump (exists).
4. Prototypes in gamepad.h; `#include "keys.h"` in gamepad_keys.c for
   playerKeys/qkey_t.

Pre-flight already done (DumpKeyGetAssign): entry confirmed, convention
confirmed, 5-byte split benign, callers enumerated.

## 6. Risks

| Risk | Mitigation |
|---|---|
| First NASM trampoline -- symbol decoration / stack offsets | Mirror client_callbacks.asm conventions exactly; the iw3sp stub is a proven template; the offsets (push eax; pushad; +0x20 frame) are fixed and documented. |
| 5-byte JMP splits IMUL | Benign -- full reimpl + `ret`, original body never runs. |
| playerKeys struct mismatch | None expected -- keys.h already defines it against 0x8F1CA0; CoD4x uses it daily. Sanity: log a known binding round-trip. |
| __usercall EAX marshalling (the 3-C class of bug) | The naked stub captures EAX as arg1 before any clobber (push eax first). |
| Behavior when inUse stale | Depends on 3-E.1 inUse tracking (done). Verify the hint flips with input device. |

## 7. Estimate
**1-1.5 sessions** -- first asm file setup adds overhead, but this stub
is the simplest (no embedded addresses) and the engine-ABI is already
in keys.h. The C reimpl + helpers are ~40 lines.

## 8. Decisions for the user
1. Confirm adding **`src/gamepad_stubs.asm`** (first NASM trampoline) +
   CMakeLists wire-up. (The earlier parallel-session stub file was
   deleted; this is the sanctioned, correct one.)
2. Reuse CoD4x's `playerKeys` (keys.h, 0x8F1CA0) directly -- agreed this
   supersedes the "defer playerKeys to 3-D" note (CoD4x already exposes
   it)?
3. Put the C reimpl + helpers in `gamepad_keys.c` (alongside the table)
   or a new `gamepad_bind.c`? (Recommend gamepad_keys.c -- same domain.)
4. Install site: extend `gp_install_keynames`, or a new
   `gp_install_bindhooks()` called from IN_StartupGamepads? (Recommend a
   new function for clarity; both run at the same init point.)

---
_Status: Phase 3-E.2 committed (1fb1053). Phase 3-E.3 analysis only --
no code. Key findings: 0x4678E0 is __usercall (EAX=localClientNum), the
naked stub is the simplest (marshal + ret, no embedded addrs), and the
engine playerKeys ABI is ALREADY exposed by CoD4x keys.h (0x8F1CA0) so
no 3-D dependency. Awaiting decisions in section 8._
