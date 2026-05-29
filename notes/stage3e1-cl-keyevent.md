# Phase 3-E.1 -- CL_KeyEvent_Hk (controller in-use tracking) -- ANALYSIS

_Written 2026-05-28. Read-only. No code, no build, no hooks. Same
methodology as Phase 3-C (disasm verify first)._

---

## 0. TL;DR

- `CL_KeyEvent_Hk`'s only job: **when a keyboard/mouse key event fires,
  set `gamePads[0].inUse = false`** (so the game flips out of controller
  mode), then forward to the engine `CL_KeyEvent`.
- **The iw3mp hook site (0x4FDCBF) is SUPERSEDED -- do NOT install it.**
  CoD4x owns the key event loop: `Com_EventLoop` (common.c:472) calls
  the engine `CL_KeyEvent` DIRECTLY, bypassing iw3mp's internal router
  (0x4FDC80) that iw3sp_mod hooks. This is the exact IN_Frame situation
  from Phase 3-C.
- Implementation = a CoD4x-native wrapper at common.c:472 (the user
  pre-authorized "modify the code directly if CoD4x owns Key_Event").
  No engine hook, no naked stub, no engine-ABI.
- **One nuance from our Path A choice:** our gamepad buttons
  (K_JOY1..16) ALSO flow through Com_EventLoop -> CL_KeyEvent, so the
  wrapper must NOT flip inUse=false for gamepad keys (guard by keycode
  range). iw3sp_mod didn't need this guard because its gamepad keys
  bypass CL_KeyEvent.

## 1. What CL_KeyEvent_Hk does (iw3sp_mod Gamepad.cpp:1103)

```cpp
void Gamepad::CL_KeyEvent_Hk(int localClientNum, int key, int down, unsigned time) {
    gamePads[0].inUse = false;                       // keyboard pressed -> not using pad
    Dvars::Functions::SetRaw(Dvars::gpad_in_use, false);
    if (Devgui::DevGui_IsActive())                   // iw3sp-only; we skip Devgui
        Devgui::DevGui_KeyEvent(localClientNum, key, down, time);
    else
        Hook::Call<void(int,int,int,unsigned)>(0x443D60)(localClientNum, key, down, time); // CL_KeyEvent
}
```
Installed in iw3sp_mod via `Utils::Hook(0x533B8F, CL_KeyEvent_Hk,
HOOK_CALL)` -> replaces the `CALL CL_KeyEvent` inside the engine's key
router with `CALL CL_KeyEvent_Hk`. Net effect: every keyboard/mouse key
event marks the pad unused. (Gamepad keys reach the engine via a
different path in iw3sp_mod -- CL_GamepadButtonEvent -- so they never
hit this and never falsely clear inUse.)

## 2. Calling convention -- CONFIRMED cdecl (disasm)

`CL_KeyEvent` entry (iw3mp 0x467EB0), from DumpStage3E.java:
```
0x467EB0: MOV EAX,[ESP+0xc]        ; arg3 (down) from STACK -- not EAX-input
0x467EB4: SUB ESP,0x404
0x467EBB: MOV EBX,[ESP+0x40c]      ; arg1 (localClientNum) from STACK
...
```
Call site (iw3mp 0x4FDCBF):
```
0x4FDCBF: CALL 0x00467eb0
0x4FDCC4: ADD ESP,0x10             ; caller cleans 16 bytes = 4 args
```
=> **plain __cdecl, 4 stack args** (localClientNum, key, down, time).
NOT __usercall (contrast CL_MouseMove). This matches CoD4x's existing
declaration `win_sys.h:179: void __cdecl CL_KeyEvent(int,int,qboolean,
unsigned)`. No regparm, no naked stub needed. LOW risk.

## 3. The CoD4x conflict -- iw3mp hook is SUPERSEDED (like IN_Frame)

- `CL_KeyEvent` is an external engine fn (win_sys.h:179); CoD4x calls
  it, doesn't define it.
- **CoD4x owns the key router**: `Com_EventLoop` (common.c:454),
  line 471-472:
  ```c
  case SE_KEY:
      CL_KeyEvent( 0, ev.evValue, ev.evValue2, ev.evTime );
  ```
  Every queued SE_KEY (keyboard from win_wndproc WM_KEYDOWN/UP, mouse,
  AND our gamepad K_JOY from gp_dispatch_buttons) is dispatched HERE,
  straight to the engine `CL_KeyEvent` (0x467EB0).
- iw3mp's internal router (0x4FDC80, containing the 0x4FDCBF call that
  iw3sp_mod hooks) is NOT on CoD4x's live path -- CoD4x's Com_EventLoop
  replaces it. **So installing GP_HOOK_CALL at 0x4FDCBF would be DEAD**
  (the engine router isn't reached), exactly like the IN_Frame hook in
  Phase 3-C.

**Conclusion:** don't hook the engine. Wrap the call at common.c:472.

## 4. The Path A sub-conflict (must handle)

In iw3sp_mod, gamepad keys do NOT pass through CL_KeyEvent (they use
CL_GamepadButtonEvent), so CL_KeyEvent_Hk safely clears inUse for any
key it sees. **In our Path A, gamepad buttons ARE delivered as
K_JOY1..16 via Com_QueueEvent -> Com_EventLoop -> CL_KeyEvent.** If our
wrapper cleared inUse on every key, our OWN gamepad button presses would
immediately clear the inUse that gp_dispatch_buttons just set -> inUse
would never stay true. BROKEN.

**Guard:** the wrapper clears inUse=false ONLY for keys OUTSIDE the
gamepad-emitted range (i.e. NOT K_JOY1..K_JOY16). Keyboard/mouse keys
clear it; gamepad K_JOY keys leave it alone.
- Edge case: the LEGACY left stick emits K_UPARROW/DOWN/LEFT/RIGHT
  (same as keyboard arrows) -- those WOULD be treated as "keyboard" and
  clear inUse. Harmless: at legacy=1, inUse isn't used for movement; at
  legacy=0 the left stick doesn't emit arrows (it's usercmd). Acceptable
  for 3-E.1; revisit if it ever matters.

## 5. Implementation plan (CoD4x-native, no engine hook)

1. New function in gamepad code (e.g. gamepad.c or a small
   gamepad_keyevent.c):
   ```c
   void __cdecl gp_cl_keyevent(int localClientNum, int key, int down, unsigned time) {
       // Clear controller-in-use for NON-gamepad keys (keyboard/mouse).
       if (key < K_JOY1 || key > K_JOY16) {
           gp_state[0].inUse = false;
           // (optional) mirror to a gpad_in_use cvar if/when we add one
       }
       CL_KeyEvent(localClientNum, key, down, time);   // engine fn (win_sys.h)
   }
   ```
2. In `common.c` Com_EventLoop, change the SE_KEY dispatch (line 472)
   from `CL_KeyEvent(...)` to `gp_cl_keyevent(...)`. One-line redirect
   to OUR wrapper. (User-authorized: CoD4x owns Key_Event, edit directly.)
3. No GP_HOOK_*, no Patch_*, no asm, no engine-ABI struct.

### Files touched (planned)
- `src/gamepad.c` (or new tiny TU): `gp_cl_keyevent` wrapper + prototype.
- `src/common.c`: 1-line call-site redirect at Com_EventLoop:472.
- (No CMakeLists change if we put the wrapper in gamepad.c.)

### Why this is the right call vs an engine hook
- CoD4x already intercepts the key loop; the engine's 0x4FDC80 router is
  bypassed. Editing common.c:472 is the live, deterministic point --
  same lesson as Phase 3-C's IN_Frame (CoD4x's 0x452A44 redirect made
  the 0x576193 hook redundant).

## 6. Methodology checklist (Phase 3-C discipline)

- [x] Disassembly verify convention BEFORE coding -- done (cdecl, §2).
- [x] Conflict check (does CoD4x own the path?) -- done (yes, §3).
- [ ] Pre-flight script: NOT needed -- no engine patch site (we edit
      CoD4x's own call). The "site" is common.c:472 (source, not bytes).
- [ ] Diagnostic Printf: add a one-shot log of inUse transitions
      (key, down, inUse) during bring-up; remove after verify.
- [ ] Backup deployed dll before deploy; smoke-test legacy=0 + legacy=1
      for zero regression; confirm inUse flips correctly (gamepad press
      -> true; keyboard press -> false).

## 7. Test plan (after implementation)

- `gpad_debug 1`-style log: press a gamepad button -> inUse=true; tap a
  keyboard key -> inUse=false; press pad again -> inUse=true.
- In-game (legacy=0): after touching the keyboard, gamepad movement
  should stop being authoritative until a pad input resumes (inUse
  gates gp_cl_mousemove). Verify the mouse/keyboard regains control
  after a keyboard tap, and the pad re-takes control on the next stick/
  button input.
- Zero regression at legacy=1.

## 8. Estimate
**1 session** (small wrapper + 1-line common.c redirect + diagnostic +
test). Lowest-risk item in Phase 3-E (cdecl, CoD4x-native, no asm/ABI).

## 9. Decisions for the user
1. Put the wrapper in `gamepad.c` (no new file) or a small new
   `gamepad_keyevent.c`? (Recommend: gamepad.c -- it's ~10 lines.)
2. Add a `gpad_in_use` cvar now (iw3sp_mod sets one), or keep inUse
   purely as internal state for 3-E.1? (Recommend: internal-only now; a
   cvar can come with the Phase 4 menu if needed.)
3. Confirm editing `common.c:472` (core CoD4x file, one-line redirect)
   is acceptable -- it's the sanctioned "CoD4x owns Key_Event" path you
   called out.

---
_Status: Phase 3-C complete. Phase 3-E.1 analysis only -- no code.
Finding: iw3mp 0x4FDCBF hook SUPERSEDED (CoD4x Com_EventLoop owns the
key loop); port CL_KeyEvent_Hk as a CoD4x-native wrapper at
common.c:472 with a K_JOY guard. Awaiting review + decisions in §9._
