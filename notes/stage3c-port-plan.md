# Phase 3-C — port plan (analog movement via usercmd_s hook)

_Written 2026-05-27 after Phase 3-B verified. **Reading + planning only.**
No code yet._

---

## 0. TL;DR

- Phase 3-C replaces Stage 3A's "left stick → arrow-key edges" hack
  with proper analog `cmd->forwardmove` / `rightmove` writes, and
  the right-stick `CL_MouseEvent` hack with `cmd->pitchmove` /
  `yawmove` writes.
- **Exactly ONE hook to install** (already accepted in the Stage 2
  address map): `HOOK_JUMP` at iw3mp `0x463D70`, our
  `gp_cl_mousemove_stub` replaces the engine's CALL to `CL_MouseMove`
  inside the usercmd builder.
- **Aim assist is NOT in scope here** -- it stays in Stage 3-D.
  Phase 3-C writes the four movement fields and lets the engine
  apply them naturally; no `viewangles[]` / `clients[0]` reads.
- The Stage 3A `Gamepad_ApplyRightStick` and `Gamepad_ApplyLeftStick`
  STAY in the build until 3-C is smoke-tested -- per user constraint
  ("نخلّيها كـfallback لو usercmd hook فشل"). They are gated behind
  a new `cl_gamepad_legacy_sticks` cvar so the user can A/B test.

## 1. Anatomy of iw3sp_mod's `CL_GamepadMove` (Gamepad.cpp 894-955)

```cpp
void Gamepad::CL_GamepadMove(Game::usercmd_s* cmd) {
    auto& gamePad = gamePads[0];
    auto& clientActive = Game::clients[0];   // engine ABI -- DEFERRED

    auto pitch = CL_GamepadAxisValue(GPAD_VIRTAXIS_PITCH);
    if (!input_invertPitch) pitch *= -1;
    auto yaw      = -CL_GamepadAxisValue(GPAD_VIRTAXIS_YAW);
    auto forward  =  CL_GamepadAxisValue(GPAD_VIRTAXIS_FORWARD);
    auto side     =  CL_GamepadAxisValue(GPAD_VIRTAXIS_SIDE);

    auto moveScale = (float)CHAR_MAX;                         // 127
    if (fabs(side) > 0 || fabs(forward) > 0) {
        auto length = (fabs(side) <= fabs(forward))
                        ? side/forward
                        : forward/side;
        moveScale = sqrt(length*length + 1.0f) * moveScale;
    }

    int forwardMove = floor(forward * moveScale);
    int rightMove   = floor(side    * moveScale);
    int pitchMove   = floor(pitch   * moveScale);
    int yawMove     = floor(yaw     * moveScale);

    cmd->rightmove    = ClampChar(cmd->rightmove    + rightMove);
    cmd->forwardmove  = ClampChar(cmd->forwardmove  + forwardMove);
    cmd->pitchmove    = ClampChar(cmd->pitchmove    + pitchMove);
    cmd->yawmove      = ClampChar(cmd->yawmove      + yawMove);

    // ----- AIM ASSIST BLOCK (lines 931-954) -- DEFERRED to Stage 3-D
    if (CG_ShouldUpdateViewAngles() && !(clientActive.snap.ps.pm_flags & PMF_FROZEN)) {
        Game::AimInput aimInput{};
        Game::AimOutput aimOutput{};
        // ... reads clientActive.viewangles, cgameMaxPitchSpeed, etc.
        // ... calls AimAssist_UpdateGamePadInput
        // ... writes clientActive.viewangles[0/1] + cmd->meleeChargeDist/Yaw
    }
}
```

**Two layers** in this function:

1. **Movement math** (lines 894-928) -- pure float arithmetic, writes
   four `signed char` fields on `cmd`. **Phase 3-C ports this.**
2. **Aim assist** (lines 931-954) -- reads `clients[0]` and writes
   `viewangles[]` + melee fields. **DEFERRED to Stage 3-D**, when
   we'll byte-verify the `clientActive_t` / `AimInput` / `AimOutput`
   struct layouts.

## 2. The hook structure: how `CL_GamepadMove` actually fires

The engine's per-frame usercmd build calls `CL_MouseMove(cmd)`
unconditionally. iw3sp_mod hijacks that call site via two pieces:

```cpp
// inside the engine's usercmd builder (host fn 0x440x in iw3sp, 0x463D10 in iw3mp)
// the original 5-byte CALL CL_MouseMove gets replaced with a JMP to:

void Gamepad::CL_MouseMove(Game::usercmd_s* cmd) {           // line 958
    auto& gamePad = gamePads[0];
    if (!gamePad.inUse) {
        // Mouse user: call the ORIGINAL engine CL_MouseMove via raw VA
        Utils::Hook::Call<void(usercmd_s*)>(0x43FA90)(cmd);
    }
    else if (Dvars::gpad_enabled && gamePad.enabled) {
        if (!Key_IsCatcherActive(KEYCATCH_CONSOLE))
            CL_GamepadMove(cmd);
    }
}
// ... and CL_MouseMove_Stub is a naked-asm trampoline that arranges
// args + jumps to CL_MouseMove. Gamepad.cpp:2330 installs
// Utils::Hook(0x4402F7, CL_MouseMove_Stub, HOOK_JUMP).
```

So the on-disk shape is:

| | iw3sp address | iw3mp address (Stage 2 verified) |
|---|---|---|
| **Original `CL_MouseMove` entry** (the engine fn that processes mouse axes into the cmd) | `0x43FA90` | `0x463490` |
| **Inside the engine's usercmd builder**: the CALL to `CL_MouseMove` we replace | `0x4402F7` | `0x463D70` |
| Host function containing that CALL site | `0x4402B0` | `0x463D10` |

**Both addresses are `accepted` in stage3d-address-map.json.** No new
Stage 2 work needed.

## 3. iw3sp_mod's other CL_GamepadMove dependencies

| Dependency | Source line | What Phase 3-C needs |
|---|---|---|
| `CL_GamepadAxisValue` | 855 | Port directly (~30 lines, pure float math + axis-map table). |
| `ClampChar` | 889 | Trivial helper (`std::clamp<int>` between -128..127). 5 lines. |
| `axisSameStick[]` | 44 (already in `gamepad_internal.h`-style code via `gp_phys_axis_e`) | Already mapped. |
| `Dvars::input_invertPitch` | dvar | New cvar `cl_gamepad_invert_pitch` (or reuse `cl_gamepad_invert_y` since semantically identical). |
| `Dvars::gpad_enabled` | dvar | Reuse existing `cl_gamepad`. |
| `Key_IsCatcherActive(KEYCATCH_CONSOLE)` | engine fn | CoD4x has its own: `cl_keys.c:238 -- bool Key_IsCatcherActive(localClientNum, mask)`. |
| `gamePads[0].inUse` | state | We set this in `gamepad_buttons.c`'s `gp_dispatch_buttons` (already done in Phase 3-B). |
| `gamePadGlobals[0].axes` (the `GpadAxesGlob` with `axesValues[6]` and `virtualAxes[6]`) | state | **NEW.** Need to populate `axesValues[]` from the polled `gp_state[0].sticks[]` + `analogs[]`. The `virtualAxes[]` binding table is Phase 3-E (bindaxis command) -- for 3-C we hardcode the default mapping (LSX→SIDE, LSY→FORWARD, RSX→YAW, RSY→PITCH, RTRIG→ATTACK, LTRIG→nothing). |
| `CG_ShouldUpdateViewAngles` | iw3sp local helper | **DEFER** -- only used in the aim-assist branch we're skipping. |
| `Game::clients[0]`, `clientActive.snap.ps.pm_flags`, `viewangles[]`, `cgameMaxPitchSpeed/YawSpeed` | engine globals | **DEFER** -- aim-assist only. Phase 3-C does pitchmove/yawmove via cmd, no direct viewangles write. |
| `AimAssist_UpdateGamePadInput`, `AimInput`, `AimOutput`, `PMF_FROZEN` | engine ABI | **DEFER** to Stage 3-D. |

**Net for Phase 3-C:** add `axesValues[6]` to our `gp_globals_t` (it's
already in `gp_axes_glob_t`-shaped layout in `gamepad_internal.h`,
but the `axesValues` array isn't declared there yet -- we need to
add it). The `virtualAxes[]` binding table starts with a hardcoded
default that mirrors the console layout.

## 4. usercmd_s field-layout fidelity (critical!)

CoD4x's `q_shared.h:1897-1909` declares an **incomplete** `usercmd_t`:

```c
#pragma pack(push, 1)
typedef struct usercmd_s {
    int   serverTime;
    int   buttons;
    int   angles[3];
    byte  weapon;
    byte  offHandIndex;
    byte  field_16;       // <-- this is forwardmove in iw3mp
    byte  field_17;       // <-- this is rightmove in iw3mp
    int   field_18;       // <-- bytes 0x18..0x1B = upmove,pitchmove,yawmove,padding
    int   field_1C;       // <-- first 4 bytes of gunPitch (a float)
} usercmd_t;
#pragma pack(pop)
```

iw3sp_mod has the full layout (Structs.hpp:597-617). **The bytes line
up** (verified by Stage 2: `CL_MouseMove` was byte-identical between
iw3sp_mod and iw3mp, and that function writes through this struct).

**Two options for accessing the movement fields:**

| | Approach | Pros | Cons |
|---|---|---|---|
| (A) | Define a private struct `gp_usercmd_t` in `gamepad_internal.h` with the iw3sp_mod-named layout, cast `(usercmd_t*) → (gp_usercmd_t*)` inside our hook. | Zero risk to other CoD4x code. Self-documenting. | Two struct definitions for the same engine memory. |
| (B) | Update `q_shared.h` to add `forwardmove/rightmove/upmove/pitchmove/yawmove/gunPitch/...` field names. | One source of truth. | Other CoD4x code uses `field_16/17/18/1C` as opaque -- a global rename could ripple through unknown call sites. |

**Recommendation: option (A).** Stage 3-C is gamepad-scoped; touching
`q_shared.h` would invite scope creep.

## 5. CoD4x integration point

CoD4x **does not own** the usercmd build pipeline -- it lives entirely
in iw3mp.exe's code. CoD4x's only usercmd touchpoints today are
read-only views (e.g. `client.h:438 -- usercmd_t cmds[128]` is the
client-side cmd ring buffer for prediction). Nothing in CoD4x writes
to `forwardmove`/`rightmove` today.

So our integration is the **hook install**, not a callsite replacement:

```c
// in gp_install_hooks() or similar, called from IN_StartupGamepads():
GP_HOOK_JUMP(IW3MP_CL_MOUSEMOVE_STUB_JMP, gp_cl_mousemove_trampoline);

// where:
extern void gp_cl_mousemove_trampoline(void);   // naked asm
extern void gp_cl_mousemove(usercmd_t *cmd);    // C: branch in/out
```

The trampoline arranges the `usercmd_t*` argument (the engine passes
it on the stack at this call site) and jumps into our C function.
**This is the first naked-asm trampoline of the port.** We'll add it
to `client_callbacks.asm` (the existing CoD4x NASM file) -- the
toolchain is already wired.

## 6. The trampoline: argument convention

The original CALL at iw3mp 0x463D70 is `call CL_MouseMove` (engine
`__cdecl`), where the caller has pushed `cmd` on the stack. The
engine assumes `CL_MouseMove(usercmd_t*)`. Our hook needs to call
either:

- `gp_cl_mousemove(cmd)` (our C function), which itself calls the
  original engine `CL_MouseMove` (at `0x463490`, a function pointer
  cast) when the mouse path is preferred, or our `gp_cl_gamepadmove`
  otherwise.

Since both targets are `__cdecl void f(usercmd_t*)`, the trampoline is
trivially "JMP gp_cl_mousemove" -- the engine's CALL has already
pushed the arg, our C function pops/preserves it via the normal cdecl
calling convention. **No naked asm needed for this specific hook.**

So `gp_cl_mousemove_trampoline` reduces to just `gp_cl_mousemove`
directly. We can install:

```c
GP_HOOK_JUMP(IW3MP_CL_MOUSEMOVE_STUB_JMP, gp_cl_mousemove);
```

Where `gp_cl_mousemove`:

```c
static void __cdecl gp_cl_mousemove(usercmd_t *cmd) {
    if (!gp_state[0].inUse || !cl_gamepad->boolean) {
        // Mouse path: call the ORIGINAL engine CL_MouseMove.
        typedef void (__cdecl *cl_mousemove_fn)(usercmd_t*);
        ((cl_mousemove_fn)IW3MP_CL_MOUSEMOVE_FN)(cmd);
        return;
    }
    if (Key_IsCatcherActive(0, KEYCATCH_CONSOLE))
        return;
    gp_cl_gamepadmove(cmd);   // the new movement function
}
```

**Verification needed:** confirm iw3mp's `CL_MouseMove` is `__cdecl`
(not `__stdcall`). Stage 2 byte-identity vs iw3sp_mod's
`Game::IN_MouseMove_t = (IN_MouseMove_t)0x594730` (typedef'd as plain
`void(*)()`) suggests `__cdecl`, but worth a 1-shot Ghidra
disassembly at 0x463490's entry to double-check the stack cleanup.

## 7. New files / changes proposed

| Path | Action | Lines (est.) |
|---|---|---:|
| `src/gamepad_internal.h` | Add `gp_usercmd_t` typedef, `gp_axes_glob_t::axesValues[6]`, prototypes for new functions. | +30 |
| `src/gamepad_movement.c` **(new)** | `gp_cl_gamepadmove`, `gp_cl_mousemove` (hook target), `gp_clamp_char`, `gp_axis_value`, `gp_install_hooks`, default `virtualAxes[]` binding table. | ~150 |
| `src/gamepad_poll.c` | Populate `gp_axes_glob_t.axesValues[]` from `gp_state[port].sticks[]` + `analogs[]` in `gp_poll_all()`. | +10 |
| `src/gamepad.c` | Register `cl_gamepad_invert_pitch` (new) and `cl_gamepad_legacy_sticks` (new, default 1). Add `gp_install_hooks()` call to `IN_StartupGamepads()`. **Do NOT delete** `Gamepad_ApplyRightStick` / `Gamepad_ApplyLeftStick` -- gate them behind `cl_gamepad_legacy_sticks`. | +20 / -0 |
| `CMakeLists.txt` | Add `src/gamepad_movement.c`. | +1 |

**Net:** one new TU + small extensions. ~210 lines added; nothing deleted from Stage 3A's stick code (kept as gated fallback per user constraint).

## 8. New cvars introduced in Phase 3-C

| cvar | default | flags | purpose |
|---|---|---|---|
| `cl_gamepad_legacy_sticks` | **1** (on by default during 3-C burn-in) | `CVAR_ARCHIVE` | When 1: use Stage 3A's stick code (CL_MouseEvent + arrow keys). When 0: use the new usercmd path. **Default flips to 0 once the new path is verified for a few sessions.** |
| `cl_gamepad_invert_pitch` | 0 | `CVAR_ARCHIVE` | Inverts the pitch axis at the usercmd level. Coexists with the existing `cl_gamepad_invert_y` (which inverts at the CL_MouseEvent layer). For 3-C burn-in we expose both; in Phase 3-E one of them gets consolidated. |

Existing `cl_gamepad_sens_look` / `cl_gamepad_sens_ads` are reused
as the axis-scale multiplier on `pitchmove`/`yawmove` (was applied
to `dx`/`dy` in CL_MouseEvent in Stage 3A).

## 9. MP-specific risks

| Risk | Severity | Notes |
|---|---|---|
| Server prediction rejects our cmd because the byte at offset 0x16/0x17 doesn't match what the unmodified engine would have written | **HIGH** | The engine builds usercmds and runs them through `PM_*` server-side. Our writes go through the SAME field the engine already writes; we just clamp/add, exactly like a keyboard-driven `+forward` would. Should be transparent to the server. **Smoke-test by joining a public CoD4x server and verifying no kicks/desync.** |
| usercmd timing -- iw3sp_mod is SP-only; in MP the cmd-build runs at a fixed rate (~125 Hz?) and the gamepad polling is decoupled. Could cause perceived sluggishness if poll < cmd rate. | medium | Stage 3A already polls every `IN_Frame` (decoupled from cmd build); same applies here. The new path just changes WHERE the polled deflection is written, not WHEN. |
| Aim-assist deferred → Phase 3-C ships without lock-on / slowdown / auto-aim, which competitive controller players expect | medium | Documented as a Stage 3-D feature. The `cl_gamepad_legacy_sticks` toggle lets controller users keep Stage 3A behavior if 3-C feels worse without aim assist. |
| `usercmd_t` byte layout assumption is wrong (CoD4x's `field_18/1C` doesn't actually map to upmove/pitch/yaw exactly the way we think) | low | Stage 2 verified iw3mp's CL_MouseMove is byte-identical to iw3sp_mod's CL_MouseMove. That function writes through this struct. If iw3sp_mod's layout were wrong, their CL_MouseMove wouldn't work. So our cast-derived layout matches by transitivity. |
| The hook at `0x463D70` is inside a function (`0x463D10`) we have not byte-verified in full -- only the call site offset (0x60) was confirmed via Stage 2 caller-trace | low | The byte at `0x463D70` MUST be `E8 ?? ?? ?? ??` (5-byte CALL rel32). If it isn't, the patch corrupts the prologue. **Pre-flight: dump 16 bytes at iw3mp 0x463D70 via a 1-shot Ghidra script before installing.** |
| Existing CoD4x mouse path competes with us -- if BOTH our hook fires AND CoD4x's mouse code writes to cmd, we double-apply | medium | iw3sp_mod gates this with `gamePad.inUse`. We do the same in `gp_cl_mousemove`: if `inUse` is false, fall through to original `CL_MouseMove`. If `inUse` is true, skip original and use gamepad path. Single source of truth. |

## 10. Phased steps inside 3-C

| Sub-phase | Goal | Build/test | Time |
|---|---|---|---|
| **3-C.1** | Add `gp_usercmd_t` + `gp_axes_glob_t.axesValues[]` + populate from poll. Write `gp_axis_value`, `gp_clamp_char`. NO HOOK YET. Build verifies. | yes | 1 hr |
| **3-C.2** | Write `gp_cl_gamepadmove` + `gp_cl_mousemove`. STILL NO HOOK. Build verifies. | yes | 1 hr |
| **3-C.3** | Pre-flight: 1-shot Ghidra dump of 16 bytes at iw3mp 0x463D70 to confirm `E8 ??...` shape. | no build | 10 min |
| **3-C.4** | Install hook in `IN_StartupGamepads` via `GP_HOOK_JUMP(IW3MP_CL_MOUSEMOVE_STUB_JMP, gp_cl_mousemove)`. Default `cl_gamepad_legacy_sticks = 1` so Stage 3A path stays primary; new path activates only when user flips it to 0. Build + deploy. | yes | 30 min |
| **3-C.5** | Smoke test (offline first): `cl_gamepad_legacy_sticks 0` then `cl_gamepad 1`, verify movement + look feel correct. **Then test in a live CoD4x MP match** for desync / kicks. | manual | per user pace |
| **3-C.6** | If 3-C.5 passes: flip default to `cl_gamepad_legacy_sticks 0`. Stage 3A stick code stays in the binary as a kill switch through Phase 3-D. | yes | 15 min |

**Total estimate: ~3 hours dev + user smoke time.** Probably one
session, possibly two if the live-MP test reveals timing issues.

## 11. What stays untouched (per user constraint)

- `Gamepad_ApplyRightStick` -- stays in `gamepad.c`, gated on
  `cl_gamepad_legacy_sticks` so user can A/B test.
- `Gamepad_ApplyLeftStick` -- same.
- `Gamepad_ReleaseMovement` -- still called on disconnect when
  legacy path is active. When new path is active, no arrow-key
  events were ever sent so nothing to release.
- All 8 existing `cl_gamepad_*` cvars.
- `IN_GamepadsMove()` dispatch order (poll → buttons → sticks)
  remains. Only the "sticks" sub-call branches on the new cvar.

## 12. Decisions awaiting user

1. **`cl_gamepad_legacy_sticks` default = 1 (Stage 3A path), or
   = 0 (new path)?** Recommendation: **1 for the first ship**, with
   a separate Phase 3-C.6 step to flip after live smoke-test.
2. **Reuse `cl_gamepad_invert_y` for pitch inversion, or introduce
   `cl_gamepad_invert_pitch` as a separate knob?** They have
   different semantics (the existing one inverts at the
   CL_MouseEvent dy negation; the new one inverts at the
   `pitchmove` clamp). Recommendation: **introduce
   `cl_gamepad_invert_pitch`** to avoid breaking users who set
   `cl_gamepad_invert_y` for the Stage 3A path -- both coexist,
   each applies only when its respective path is active.
3. **Aim assist confirmation:** Phase 3-D as planned (separate
   session). Confirmed?
4. **Pre-flight Ghidra dump in 3-C.3:** automate with a tiny
   headless script, or eyeball it in Ghidra GUI? Recommendation:
   **3-line headless script** -- consistent with how we've been
   working in Stages 2 and 3-A.

---
_Status when this file was written: Phase 3-B complete (commit
00b37c4, verified by user). Phase 3-C not started. Awaiting user
review of this plan + answers to section 12 before code begins._

---

## 13. Phase 3-C.4 crash + root cause (2026-05-28) -- ARCHITECTURAL

### Timeline
1. First hook attempt: `GP_HOOK_CALL(0x463D70, gp_cl_mousemove)` using
   the **raw** `SetCall`. **Crash on launch** -- root cause: CoD4x's
   `SetCall`/`SetJump` do NOT self-unprotect `.text`; they assume the
   caller is inside `Patch_MainModule`'s VirtualProtect window. Our
   hook installs from `IN_StartupGamepads` (outside that window).
   **Fix:** added self-unprotecting `Patch_SetCall`/`Patch_SetJump`
   in `sys_patch.c`; pointed `GP_HOOK_CALL/JUMP` macros at them.
   Launch fixed.
2. Second attempt (self-unprotect fix in): launches fine, menus fine,
   but **crashes in-match when moving the controller**.

### Root cause (proven by WER + disassembly)
- WER Application-error log: `Faulting module iw3mp.exe, exc
  0xC0000005, offset 0x000635CB` -> VA `0x4635CB`, INSIDE the engine's
  `CL_MouseMove` (0x463490 + 0x13B). The crash is in engine code, not
  our DLL.
- **iw3mp's `CL_MouseMove` is `__usercall`, not `__cdecl`.** The sole
  caller (0x463D70 -- verified the ONLY call/jump ref to 0x463490)
  sets up:
  ```
  0x463D6D: PUSH EDI        ; arg2 = cmd   (stack)
  0x463D6E: MOV  EAX, ESI   ; arg1 = client (EAX)
  0x463D70: CALL 0x463490
  0x463D7D: ADD  ESP, 4     ; caller cleans 1 stack arg
  ```
  Entry reads EAX: `MOV EDI, EAX`. The movement path does
  `IMUL EDI,EDI,0x258 ; ... CMP [EDI+0xB0]` -- i.e. EAX is the local
  client index used to index the client array.
- Our `__cdecl` hook clobbered EAX before forwarding to 0x463490, so
  the engine indexed the client array with garbage -> wild pointer ->
  fault at 0x4635CB. The fault only triggers on the movement code path
  (non-zero stick/mouse delta), which is exactly why it crashed "only
  when moving" and why menus/idle were fine.
- Why iw3sp_mod didn't hit this: SP's `CL_MouseMove` (0x43FA90) has one
  implicit client, no EAX arg, so iw3sp_mod's `__cdecl(usercmd_s*)`
  was correct for SP. **MP added a client arg in EAX** -- an
  SP-vs-MP ABI difference.

### Fix (no asm)
- `gp_cl_mousemove` declared `__attribute__((regparm(1)))` with
  signature `(int client, gp_usercmd_t *cmd)`. GCC's regparm(1) places
  arg1 in EAX, arg2 on the stack, caller-cleaned -- exactly iw3mp's
  convention. The fallback forwards `(client, cmd)` so EAX is preserved
  into the original CL_MouseMove. `gp_cl_gamepadmove` is unchanged (it
  needs only `cmd`).
- Builds clean; deployed SHA `258A8C07...` (diagnostic build prints
  `client=` too). Awaiting in-match move test.

## 14. ARCHITECTURAL RULE for ALL future hooks (Stage 3-D / 3-E)

**iw3mp uses `__usercall` (register args, commonly EAX as the first
arg) for many engine functions.** A hook that assumes `__cdecl` will
silently clobber a register arg and crash inside the engine -- often on
a delayed/conditional code path (as here: only on movement), making it
look unrelated to the hook.

**MANDATORY before installing any new engine hook:**
1. Disassemble BOTH the call site (what regs/stack are set up before
   the CALL) AND the callee entry (which regs it reads before writing).
   Reuse `DumpCallSite.java` / `DumpMouseMoveCallers.java` patterns.
2. Confirm whether args are in EAX/ECX/EDX (regparm/usercall) or purely
   on the stack (cdecl/stdcall), and who cleans the stack (caller =
   cdecl/regparm; callee `RET n` = stdcall/thiscall).
3. Match the C declaration's convention to the engine's:
   - args fully on stack, caller-cleaned -> `__cdecl`
   - first arg(s) in EAX(/EDX/ECX), caller-cleaned -> `__attribute__((regparm(N)))`
   - callee-cleaned stack -> `__attribute__((stdcall))`
   - genuinely custom (e.g. ECX `this` + odd cleanup) -> naked asm
     trampoline.
4. For hooks that REPLACE a `CALL` and may forward to the original,
   forward with the SAME convention so every register arg is preserved.

This must appear in the PR documentation as a known engineering
constraint of porting SP (iw3sp) hooks to MP (iw3mp).
