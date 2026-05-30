# Session status -- living file

Updated after every execution step. Purpose: never again confuse
"code presented for review" with "code actually built & deployed".

_Last updated: 2026-05-28, after Phase 3-E.2 verified + committed; 3-E.3 planning next._

## Phase 3-E progress (2026-05-28)

Roadmap: full chain (user decided slice insufficient -- CoD4 MP menus
need real engine-keynum integration). Order: 3-E.1 -> 3-E.2 -> 3-E.3
-> 3-E.4 -> 3-E.5 -> 3-E.6.

- **3-E.1 (inUse tracking) -- DONE + committed (72d1241).** Wrapped
  CoD4x's Com_EventLoop SE_KEY dispatch (common.c:472) with
  `gp_cl_keyevent`: clears inUse=false for keyboard/mouse keys, passes
  K_JOY1..16 through. The iw3mp 0x4FDCBF hook was SUPERSEDED (CoD4x owns
  the key loop) -- wrapped at source instead. Verified.
- **3-E.2 (keyName table) -- DONE + committed (1fb1053).** New
  `gamepad_keys.c`: combined table (95 stock copied at runtime + 16
  gamepad + null), `Patch_SetPtr` x3 on the engine reverse-lookup
  operands (0x46777D/85, 0x467837 -- pre-flight confirmed all == 0x726F48),
  + `gp_keynum_to_name` fallback wired into CoD4x's reimplemented
  `Key_KeynumToString` (cl_keys.c). Verified: "111 entries installed",
  `bind BUTTON_A` accepted, forward display shows BUTTON_A not 0x01.
  - **Conflict found + handled:** CoD4x reimplements Key_KeynumToString
    (hardcoded 0x726F48), so the engine Set patches only cover the
    reverse lookup; forward display needed the cl_keys.c fallback. Two
    consumers, two owners.
  - **Timing caveat (deferred to 3-E.4):** gp_install_keynames runs in
    IN_StartupGamepads, AFTER config exec -- so a `bind BUTTON_A` saved
    in config_mp.cfg may not resolve on next launch (binding lost).
    Fix = move the keyname install earlier, in 3-E.4.
- **Deployed:** SHA D112EDDAC042C72FC7F794E847AD1E7288696E4AC865B2DA7C60C89D655BC7D0,
  size 2,629,632. Backups: .phase3e1-verbose (480B28AE),
  .phase3e1-clean (8D563CC7).

- **3-E.3 (Key_GetCommandAssignment) -- DONE + committed (e2d6978).**
  First naked-asm trampoline (gamepad_stubs.asm) installed at iw3mp
  0x4678E0. C reimpl in gamepad_keys.c reads engine playerKeys via
  CoD4x's keys.h (0x8F1CA0) -- no new ABI reverse-engineering. Engine
  keynum ranges (BUTTON_A=0x1..DPAD_RIGHT=0x17) used. Console log
  "Gamepad bind hooks installed (Key_GetCommandAssignment @ 0x4678E0)"
  on startup. `bind BUTTON_A` accepted; reverse lookup returns the
  controller binding. Verified, no crash.
  - Hazard noted: a stray `*/` inside `BUTTON_* / DPAD_*` comments
    closes the C comment block -> "unknown type name 'DPAD_'" compile
    error. Hit it twice (3-E.2 and 3-E.3); now in the project's known
    pitfalls list.
  - Path A tension persists: `bind BUTTON_A` won't fire from a
    physical press (we still emit K_JOY1). Lookup foundation is in
    place; activation needs 3-E.4b (migration).

- **3-E.4a (Key_SetBinding hooks x3) -- DONE + committed (0c5be14).**
  3 naked-asm trampolines in gamepad_stubs.asm (stub01/02 identical
  pattern, stub03 with EDX). HOOK_CALL via Patch_SetCall (cleaner than
  iw3sp_mod's HOOK_JUMP for these CALL sites: engine's CALL push gives
  us the jump-back automatically, stubs end with plain `ret`). All 3
  forward to engine inner binder (0x4678b0) -- the SAME function the
  original CALLs targeted, so no Ghidra resolve was needed. Hk in
  gamepad_keys.c flips `gpad_buttonConfig` to "custom" for gamepad
  keynums (BUTTON_*/DPAD_*). Verified: cvar flips on `bind BUTTON_A`,
  JOY1 binds unaffected.
  - Persistence timing (cfg-exec-before-install) is still open --
    moving the install to Com_Init is the 3-E.5 task.
- **3-E.4b (K_JOY -> engine keynum migration) -- PLANNING NOW.**
  Replaces the dispatch table in gamepad_buttons.c so a physical A
  press emits GAME_K_BUTTON_A (0x1) instead of K_JOY1 (~207). Gated
  by new `cl_gamepad_legacy_input` cvar (default 1 = Phase 3-B
  behavior; 0 = engine keynums for the full chain to "wake up"). The
  moment `bind BUTTON_A` actually fires from a physical press -- AND
  the engine UI handler finally sees the right keynums in menus
  (potentially solving the 3-E.0 menu-nav problem). ~1 session, no
  asm. See notes/stage3e4b-input-migration.md.

## Phase 3-C COMPLETE (2026-05-28) -- analog movement + working look

**Result:** controller analog movement (left stick) + look (right stick)
work together in MP, zero crash, zero regression. Reached via four
architectural fixes, each diagnosed from hard evidence:

1. **Self-unprotecting patch** -- CoD4x's raw `SetCall`/`SetJump` write
   `.text` directly and assume the caller is inside `Patch_MainModule`'s
   VirtualProtect window. Our hook installs from `IN_StartupGamepads`
   (outside it) -> launch crash. Fix: added `Patch_SetCall`/
   `Patch_SetJump` (self-unprotect) in `sys_patch.c`; `GP_HOOK_CALL/JUMP`
   route through them.
2. **regparm(1) calling convention** -- iw3mp's `CL_MouseMove` is
   `__usercall`: arg1 (client index) in EAX, arg2 (cmd) on the stack.
   Our `__cdecl` hook clobbered EAX -> forwarded a garbage client index
   -> engine faulted at 0x4635CB on the movement code path (only when
   moving). Proven via WER event log (iw3mp.exe +0x635CB, 0xC0000005)
   + disassembly of the single call site (0x463D70) and callee entry
   (0x463490). Fix: declare `gp_cl_mousemove` and the forward typedef
   `__attribute__((regparm(1)))` with `(int client, gp_usercmd_t*)`.
3. **Hybrid look (Option A)** -- right-stick look was dead at legacy=0
   because `gp_cl_gamepadmove` bypassed `CL_MouseMove`, orphaning the
   `CL_MouseEvent` accumulator that `Gamepad_ApplyRightStick` fills
   (the actual view-rotation lives in the deferred aim-assist/turn-rate
   branch that writes `clientActive.viewangles`). Fix: dispatcher calls
   the original `CL_MouseMove` for the look, THEN `gp_cl_gamepadmove`
   for movement (forwardmove/rightmove only). pitchmove/yawmove dropped
   -- they return in Stage 3-D with the viewangles turn-rate port.
4. **Left-stick gate** -- `Gamepad_ApplyLeftStick` (arrow-key movement)
   now fires only when `cl_gamepad_legacy_sticks==1`, so legacy=0 does
   not double-apply movement on top of the analog usercmd path.
   `Gamepad_ApplyRightStick` stays unconditional (look source in both
   modes).

**Behavior:**
- `cl_gamepad_legacy_sticks 1` (DEFAULT): identical to Phase 3-B
  (CL_MouseEvent look + arrow-key movement). Safe fallback.
- `cl_gamepad_legacy_sticks 0`: analog usercmd movement + CL_MouseMove
  look. User opts in manually.

**Files changed in Phase 3-C (uncommitted, awaiting final smoke-test):**
- `sys_patch.{c,h}` -- Patch_SetCall / Patch_SetJump.
- `gamepad_hooks.h` -- macros route through the self-unprotect variants.
- `gamepad_internal.h` -- gp_usercmd_t, axesValues[], regparm(1)
  prototype + convention note, Phase 3-C externs.
- `gamepad_move.c` (NEW) -- movement writer + dispatcher (regparm(1),
  hybrid).
- `gamepad.c` -- 2 cvars (cl_gamepad_legacy_sticks default 1,
  cl_gamepad_invert_pitch), hook install, left-stick gate.
- `CMakeLists.txt` -- + src/gamepad_move.c.

**Clean build deployed (debug Printf removed):** SHA
`0F4DCA2C9F725F45CED0D959B36E736CBD42F728E6765B11596E81F5587DD880`,
size 2,627,584. Verified "hook fired" string absent from the DLL.

**Backups:** `.dead-safe` (B92F94BB, pre-hook), `.phase3c4-regparm-ok`
(258A8C07, look dead), `.phase3c-hybrid-debug` (6FD3B31B, working +
debug Printf), current = clean.

**`cl_gamepad_invert_pitch`** is registered but DORMANT in the hybrid
(look goes through CL_MouseMove, which uses cl_gamepad_invert_y via the
Stage 3A right-stick path). It returns to active duty in Stage 3-D when
look moves to the usercmd/viewangles path.

**Architectural docs:** stage3c-port-plan.md sections 13-14 + the master
stage3-port-plan.md risk register record the __usercall + self-unprotect
constraints as MANDATORY pre-checks for every future hook (Stage 3-D/3-E).

**Final clean build (no Printf) smoke-tested by user: PASS.** Committed
as `22eb6b4` (CoD4x_Client_pub), tag `phase3c-complete`. Deployed SHA
`0F4DCA2C...587DD880`.

## Roadmap re-ordered (2026-05-28, user decision)

Priority driven by user need (controller in the main menu) + risk:

1. ✅ **Phase 3-C** complete (analog movement + hybrid look).
2. 🔄 **Phase 3-E NEXT** — BUTTON_* engine integration + bindings +
   key-name table + menu navigation. Solves the controller-in-menu
   problem. Hooks involved (from address map, all `accepted`):
   keyName table ptrs (0x4676F0+0x8D/+0x95, 0x4677C0+0x77),
   GetLocalizedKeyName calls (0x4677C0+0x6F, 0x475DC0+0x91),
   Key_GetCommandAssignment (0x4678E0), Key_WriteBindings (0x4FFB0F),
   Key_SetBinding x3 (0x5529B8/CB/E3), CL_KeyEvent (0x467EB0),
   CL_KeyEvent_Hk (0x4FDCBF). NOTE: each needs the same calling-
   convention pre-check (disassemble call site + entry) that Phase
   3-C taught us -- several are HOOK_JUMP mid-function and will need
   naked-asm trampolines (iw3sp_mod uses __declspec(naked) for them).
3. ⏳ **Phase 4** — Menu UI: separate Gamepad settings page + presets
   + `cl_gamepad_aimassist` cvar as a UI toggle (no effect yet).
4. ⏳ **Phase 3.5** — 3 deferred hooks (Player_UseEntity,
   UI_RefreshStub, reload-hint offset, CL_MouseEvent_Stub 0x5947A8).
5. ⏳ **Phase 3-D LAST** — Aim Assist: port the AimAssist_* subsystem
   (476 lines) + clientActive.viewangles turn-rate (restores the
   usercmd-native look + pitchmove/yawmove + cl_gamepad_invert_pitch),
   activates the Phase 4 toggle. Heaviest engine-ABI work; done last
   so the playable controller ships first.
6. ⏳ **Phase 5** — Community PR.

## CoD4x_Client_pub (controller work -- active)

| Item | State |
|---|---|
| Last commit (pushed) | `0081a30` feat(gamepad): analog sticks (Stage 3A) + release-on-disconnect + stick debug logging |
| `gamepad.c` on disk | == `0081a30`. git status clean. |
| Last build | `cod4x_021.dll` 2,624,000 bytes -- **Stage 3A build** (first build containing the sticks) |
| Last deployment | deployed to `game-test\fake-appdata\...\bin\cod4x_021\cod4x_021.dll` |
| Deployed SHA256 | `0A9651DA32459248EA045277C205E9F52E6EA97D55AA84809A163E00F3EC1078` |
| strings check | `Stick R:` / `Stick L:` present -> Stage 3A confirmed in the binary |
| Backups in place | `.stage-a-pre`, `.commit1-pre`, `.commit2-pre`, `.drift-debug-pre`, `.stage3a-debug-pre` |

## Deployed feature state

- Buttons + triggers (Commit 2), trigger drift debug.
- Stage 3A sticks: right stick -> view via `CL_MouseEvent`; left stick ->
  8-way movement via arrow-key events; release-on-disconnect for all
  inputs; temporary `cl_gamepad_debug` logging for triggers + both sticks.

## Pending test (user)

- **Right-stick look + left-stick movement.** Now actually testable --
  the stick code is in the deployed binary (verified). Test: in-game,
  `\cl_gamepad 1` + `\cl_gamepad_debug 1`, move the right stick slowly,
  screenshot the console (`Stick R:` lines).
- Left stick will be "silent" in-game by design: arrow keys are not bound
  to movement in `config_mp.cfg` (no defaults added, per decision). The
  `Stick L:` debug lines still confirm it is reading + edge-detecting.

## Pending code (NOT written)

- **Trigger hysteresis fix** (`PRESS=55`, `RELEASE=30`) -- still gated on
  the trigger-drift test result.
- Removal of the temporary `cl_gamepad_debug` logging once sticks +
  triggers are settled.

## Stage 3D Phase 1 -- RE toolchain installed (2026-05-23)

- **JDK** Temurin 21.0.11+10 LTS at `D:\Cod4Project\tools\jdk21\`
  (327.9 MB). SHA verified.
- **Ghidra** 12.1 PUBLIC at `D:\Cod4Project\tools\ghidra\` (862 MB,
  5,215 files). SHA `aa5cbcbb...8a6f302` verified.
- **First-run test passed** -- `ghidraRun.bat` with
  `JAVA_HOME=tools\jdk21` launches javaw cleanly. Main window:
  "Ghidra: NO ACTIVE PROJECT". No errors. Java memory 381 MB at idle.
- Both tools are portable; nothing installed system-wide; PATH and
  JAVA_HOME set per-session.
- Phase 1 deliverables in place: `notes\stage3d-phase1-setup.md`
  (hook inventory + workflow) and `notes\stage3d-address-map.json`
  (skeleton, 19 active sites awaiting `iw3mp_addr`).
- **Stage 2 partial (2026-05-23):** Ghidra project
  `tools\ghidra-projects\CoD4-Binary-Diff` created via `analyzeHeadless`
  (much faster than GUI; identical result). Both binaries imported:
  - `iw3mp.exe`: 8,521 functions / 315 externals -- clean analysis (95s).
  - `iw3sp.exe`: **only 240 functions** -- BAD. Root cause confirmed:
    iw3sp.exe is **SecuROM v7 packed** (extra `.bind` section at
    0x2101000, entry point 0x021013db inside `.bind`). Ghidra static
    analysis cannot see the compressed `.text` body.
- **Critical workaround found:** `iw3sp_mod.exe` has the identical
  section layout as retail iw3sp (`.text`@0x401000, etc.) but **no
  `.bind` section** -- it IS retail iw3sp.exe unpacked. JerryALT's
  hook addresses (0x594913 etc.) are calibrated against this unpacked
  binary. So our Version Tracking source must be `iw3sp_mod.exe`, NOT
  retail `iw3sp.exe`.
- **Stage 2 reference binary imported (2026-05-23):**
  `iw3sp_mod.exe` (3,248,640 B, SHA `EDED51AC...DAA6B`) imported into
  the same Ghidra project + autoanalyzed (60s). **7,906 functions /
  269 externals** -- a healthy 33x more than the 240 we got from
  packed retail iw3sp.exe. Confirms iw3sp_mod IS retail iw3sp
  unpacked.
- **All 19 hot addresses resolved** in iw3sp_mod.exe -- every one is
  inside an identified function. Filled into
  `stage3d-address-map.json` (`iw3sp_mod_function`,
  `iw3sp_mod_function_entry`, `offset_in_function` columns).
- **Insight: only ~14 unique functions** to match (several hooks
  share a function -- e.g. all 3 `Key_SetBinding` stubs live in
  `FUN_0056f690`).
- One address (`0x5947A8`) shows a negative offset relative to its
  reported function -- flagged `needs_review` for Version Tracking.
- **Stage 2 Version Tracking complete (2026-05-26):** User created VT
  session `CoD4_SP_vs_MP` via Ghidra GUI -- 9 matchsets, **35,039 total
  matches**. A headless lookup script (`LookupVTMatches.java`) extracted
  matches for the 14 unique hot functions.
- **Stage 2 finish complete (2026-05-26):** Two follow-up headless
  scripts ran end-to-end:
  - `Stage2Finish.java` (V1, log + .done in
    `tools/ghidra-projects/`): byte-verified the 4 Implied candidates
    (promoted 2 -> accepted, rejected 2) and fingerprinted the 4
    NO MATCH source functions against all 8,206 iw3mp functions
    (string + named-callee + size scoring). Found 2 strong matches.
  - `Stage2FinishV2.java` (V2): callers-of-known-targets discovery
    + relaxed fingerprint + 0x5947A8 gap resolution. Resolved 2 more.
- **Final Stage 2 tally (out of 19 hooks):**
  - **17 ACCEPTED** -- iw3mp_addr trustworthy.
  - **1 function-only** (0x40A0B0 reload-hint: function 0x4237B0
    confirmed by 4/4 strings shared, but offset 0xE0 invalid in iw3mp
    -- needs byte-pattern scan inside 0x4237B0).
  - **2 no_match** (0x4F92D7 Player_UseEntity, 0x5653CA UI_RefreshStub
    -- non-critical, no fingerprint).
  - **1 needs_manual** (0x5947A8 CL_MouseEvent_Stub -- sits in iw3sp
    gap; calls 0x43F920).
- **Discoveries logged in `stage3d-address-map.json`:**
  - 0x43FA90 CL_MouseMove -> **0x463490** (insn count exact 270)
  - 0x4402B0 CL_MouseMove_Stub -> **0x463D10**, hook site **0x463D70**
  - 0x443D60 CL_KeyEvent -> **0x467EB0** (5/5 strings shared; earlier
    byte reject was just register-allocation diff between SP/MP builds)
  - 0x533B50 -> 0x4FDC80 promoted accepted (21-byte prefix)
  - 0x594880 IN_Frame -> 0x576100 promoted accepted (sizes 335 vs 338)
- **Next session:** Proceed to **Stage 3 -- port Gamepad.cpp into
  CoD4x_Client_pub** using the 17 verified hooks. Defer the 3 stragglers
  (reload-hint offset, Player_UseEntity, UI_RefreshStub) to Stage 3.5
  polish pass -- none block first playable controller build.

## Stage 3 planning complete (2026-05-26)

- **Plan document:** `notes/stage3-port-plan.md` -- comprehensive
  read+plan pass over `tools/iw3sp_mod-ref/src/Components/Modules/
  Gamepad.{cpp,hpp}` (2,567 lines total, 90 funcs, 21 hook sites, 12
  naked-asm stubs).
- **Section-by-section anatomy** mapped (data tables 1-189, aim assist
  351-826, polling core 1677-1820, IN_Frame hook 1898-1959, ctor with
  all hook installs 2238-2351).
- **Dependencies inventoried:** 141 unique `Game::` symbols, 29 dvars,
  Utils::Hook / Utils::String / Dvars::Register / Command::Add /
  Events::OnDvarInit -- each mapped to a CoD4x equivalent (SetCall /
  SetJump / Patch_Memset / Cvar_Register* / Cmd_AddCommand / va()).
- **Build-system gap analyzed:** premake5/MSVC C++20 vs CMake/MinGW C99.
  Net cost: ~10-15 % of lines reshape (templates expanded, std::function
  removed, 12 naked stubs externalized to .asm file). Logic 1:1.
- **5-phase port plan (3-A...3-E)** + Stage 3.5 polish. Total estimate
  ~6 sessions. Phase 3-A is infrastructure-only (no behavior change);
  Phase 3-B is first playable controller build (poll + buttons via
  engine Key_Event path); 3-C movement+look replaces Stage 3A; 3-D aim
  assist; 3-E bindings/configs/key-names.
- **4 user decisions pending** (aim-assist default state, cheat-code
  port scope, Discord integration scope, GPL-3 attribution wording in
  new files) -- listed in section 7 of the port plan.
- **Next session:** on green light, begin Phase 3-A (create
  `gamepad_addrs.h`, `gamepad_engine.h`, `gamepad_types.h`, add
  `Patch_SetPtr` helper to `sys_patch.c`; no hooks installed; verify
  dll still builds and loads).

## Stage 3-A executed (2026-05-26, code not yet built)

- **User decisions captured** in `notes/stage3-port-plan.md` section 7:
  - aim assist default = **OFF** (user opt-in)
  - IW Code cheat = **skipped**
  - Discord rich-presence = **deferred** to Phase 4
    (`notes/next-phase-goals.md` updated)
  - GPL-3 attribution header = approved boilerplate (in every new
    `gamepad_*.c/.h`)
- **Files created (not built yet):**
  - `CoD4x_Client_pub/src/gamepad_internal.h` -- types, enums,
    constants, engine-ABI `GAME_K_*` keynums, POD structs, 14 iw3mp
    hook-site `#define`s.
  - `CoD4x_Client_pub/src/gamepad_hooks.h` -- 4 macros
    (`GP_HOOK_JUMP`, `GP_HOOK_CALL`, `GP_HOOK_SET_PTR`, `GP_HOOK_NOP`).
- **Files modified (not built yet):**
  - `CoD4x_Client_pub/src/sys_patch.h` -- added `Patch_SetPtr`
    prototype.
  - `CoD4x_Client_pub/src/sys_patch.c` -- added `Patch_SetPtr` body
    (self-unprotecting pointer overwrite -- matches `Utils::Hook::Set<T*>`).
- **Files NOT touched:** `gamepad.c`, `gamepad.h`, `win_input.c`,
  `CMakeLists.txt`. Stage 3A deployed binary still runs identically
  until Phase 3-B.
- **Build (2026-05-27):** clean rebuild via MinGW/CMake -- 88 s,
  exit 0, 0 errors, 0 new warnings. `cod4x_021.dll` size unchanged
  at 2,624,000 B (Patch_SetPtr fits in alignment slack since no
  caller yet). New SHA256
  `7F48249D72C73CDA5FEEBEFB13639E9E00CC65F594B7B9765527377322308C98`.
  Backup of prior deploy saved as `cod4x_021.dll.stage3a-final`
  (SHA `0A9651DA...F3EC1078`).
- **Smoke test (user, 2026-05-27):** verified in-game -- title bar
  "Call of Duty 4 X", launches fine, controller works, sticks +
  buttons identical to prior Stage 3A behavior. **Zero regression.**
- **Stage 3-A status: COMPLETE.** Green light for Phase 3-B in the
  next session.

## Phase 3-B started (2026-05-27, planning only)

**Goal originally framed by user:** "install IN_Frame_Hk (1 of 14
hooks) only."

**Key planning findings:**

- `IN_Frame_Hk` in iw3sp_mod is a 3-line adapter that calls
  `RawMouse::IN_MouseMove()` + `IN_GamePadsMove()`. The hook target
  iw3mp `0x576193` originally CALLed engine `IN_MouseMove` (iw3sp
  VA `0x594730`).
- **CoD4x already redirects iw3mp's `IN_Frame` wholesale** via
  `SetCall(0x452A44, IN_Frame)` (sys_patch.c:1152) -- CoD4x's own
  `IN_Frame` in `win_input.c:438` already calls `IN_GamepadsMove()`
  (the existing Stage 3A entry). **Therefore: hook at `0x576193`
  is superseded -- DO NOT install.**
- Phase 3-B real work = rewriting the body of `IN_GamepadsMove()` in
  `gamepad.c` to use the iw3sp_mod state machine
  (`gp_state_t` + `GPad_UpdateAll` polling tree), keeping the
  same C-symbol entry point so CoD4x's `IN_Frame` is unaware.
- **Path-A vs Path-B fork:** Path A delivers button events via
  `Com_QueueEvent(SE_KEY, ...)` (matches Stage 3A and adds zero new
  engine-ABI deps). Path B writes engine `playerKeys[]` directly +
  calls `Cbuf_AddText` by VA (iw3sp_mod-faithful but adds many
  byte-verification tasks). **Recommendation: Path A for 3-B.**
  Engine-ABI deep dive deferred to Stage 3-D (aim assist needs it
  anyway).
- New files for 3-B: `gamepad_poll.c` (~200 lines, pure XInput
  polling logic) + `gamepad_buttons.c` (~120 lines, Path A
  delivery). `gamepad.c` body of `IN_GamepadsMove` rewritten to call
  them. Existing `gamepad_internal.h` / `gamepad_hooks.h` untouched
  in 3-B (they're for Path B / future phases).

**4 decisions awaiting user before code is written:**
1. Confirm Path A (vs B) for Phase 3-B.
2. Stub `UpdateTheButtonAHint` (menu alignment) -- empty stub +
   `TODO: Phase 4` comment, OK?
3. Keep `K_JOY1..K_JOY16` button mapping (Stage 3A), OK?
4. Confirm: **no hook install in Phase 3-B**, CoD4x's `0x452A44`
   redirect is sufficient.

**Full report:** delivered inline in the session chat; will move into
`notes/stage3-port-plan.md` Phase 3-B section after decisions land.

## Phase 3-B build + deploy (2026-05-27)

- **Decisions confirmed by user:** Path A (Com_QueueEvent), stub
  UpdateTheButtonAHint, K_JOY1..16 mapping preserved, zero new hooks.
- **Files written:**
  - `CoD4x_Client_pub/src/gamepad_poll.c` -- 358 lines, XInput polling
    + edge-detection state machine (mirrors iw3sp_mod GPad_Check,
    GPad_RefreshAll, GPad_UpdateAll/Sticks/Digitals/Analogs/SticksDown,
    GPad_GetStick/Button, IsButtonPressed/Released/RequiresUpdates).
  - `CoD4x_Client_pub/src/gamepad_buttons.c` -- 163 lines, Path A
    dispatch (gp_dispatch_buttons + gp_release_all), K_JOY1..K_JOY16
    table verbatim from Stage 3A.
- **Files modified:**
  - `gamepad_internal.h` -- IW3MP_IN_FRAME_CALL marked SUPERSEDED;
    Phase 3-B extern API (gp_state[], gp_raw[], gp_poll_all, ...).
  - `gamepad.c` -- IN_GamepadsMove body cut to ~30 lines (poll ->
    dispatch -> sticks). 8 cl_gamepad_* cvars preserved verbatim.
    Stage 3A stick code (Apply Right/Left, ReleaseMovement) untouched.
    Empty UpdateTheButtonAHint stub with TODO Phase 4.
  - `CMakeLists.txt` -- +2 lines (the two new TUs).
- **Build (2026-05-27):** clean rebuild 76 s, exit 0, 0 errors,
  0 new warnings. Only the pre-existing _Direct3DCreate9@4 stdcall-
  fixup linker warning, plus the pre-existing CMake-deprecation
  warnings from the external dependencies. All 3 gamepad TUs
  compiled cleanly.
- **Deploy:** new SHA256
  `03ED4012496CC0292CBF152B305C520B54E28AB0811D26DF234CFB9C4674F9BF`,
  size 2,624,512 B (+512 vs Stage 3-A = one PE alignment unit
  absorbing the net new code). Prior deploy backed up as
  `cod4x_021.dll.stage3b-pre`.
- **git:** master commit `00b37c4` pushed to
  `Ab7i/CoD4x_Client_pub`. 5 files changed, +696 / -194 lines.
- **Exit gate (user smoke-test):** awaiting verification of (a)
  Stage 3A regression check (all buttons + both sticks); (b) Path A
  confirmation via `bind JOY1 "say hello..."`; (c) hot-plug
  detection within ~0.5 s.

## Phase 3-B verified (2026-05-27, user smoke-test)

- User tested with `\bind AUX1 "say hello from controller"` then
  pressed A on the controller -- "hello from controller" appeared in
  chat. Path A delivery confirmed end-to-end.
- **AUX* keyname note:** CoD4x's binding command resolves both
  `JOY1..JOY32` and `AUX1..AUX16` to the same underlying engine
  keynum range for gamepad-class events. Our dispatch fires
  `Com_QueueEvent(SE_KEY, K_JOY1, ...)` which the engine routes
  through Key_Event. The bind-name parser accepts either alias, so
  `bind AUX1 ...` and `bind JOY1 ...` both work. Documented for
  future user instructions.
- **Stage 3-B status: COMPLETE.** Code commit `00b37c4` pushed in
  CoD4x_Client_pub (no separate "verified" commit -- the code is
  unchanged from the build commit; verification recorded here in
  the notes repo).

## Phase 3-C planning starting (2026-05-27)

**Goal:** replace Stage 3A's left-stick-as-arrow-keys with proper
analog movement via `usercmd_s->forwardmove / rightmove` and route
the right stick through usercmd viewangles (or keep CL_MouseEvent
if that's cleaner for MP).

**Read-only scope this session:** anatomy of iw3sp_mod's
`CL_GamepadMove` + `CL_MouseMove` + hooks list, layout of
`usercmd_s`, integration site inside CoD4x, MP-specific risks
(usercmd timing vs server prediction), phased port plan.

**Important constraint from user (preserved verbatim):**
> لا تحذف Stage 3A الـleft stick logic قبل ما تختبر بديلها -- نخلّيها
> كـfallback لو usercmd hook فشل.

So the Stage 3A `Gamepad_ApplyLeftStick` stays in gamepad.c during
3-C development; we either gate it behind a cvar or only remove it
after the new path is smoke-tested in a live MP match.

## Phase 3-C.3 pre-flight done + Phase 3-C.1 + 3-C.2 verified (2026-05-27)

- **Pre-flight (3-C.3):** `tools/ghidra-projects/DumpCallSite.java`
  dumped 16 bytes at iw3mp `0x463D70`:
  - `E8 1B F7 FF FF | D9 05 D8 4F C8 00 D9 C0 83 C4 04`
  - opcode `E8` (CALL rel32), rel32 = -2277 -> target `0x463490`
    (== expected engine CL_MouseMove). Bytes after the call show
    `ADD ESP, 4` => `__cdecl` calling convention confirmed.
  - VERDICT = PASS. Safe to install 5-byte JMP rel32 at this address.
- **Phase 3-C.1 (types + cvars):** `gp_usercmd_t` typedef
  (pragma-pack(1), full layout from iw3sp_mod's Game::usercmd_s),
  `axesValues[6]` field added to `gp_axes_glob_t`,
  `cvar_t` forward-declared in `gamepad_internal.h`, four shared
  cvars made non-static + extern, two new cvars registered in
  `gamepad.c` (`cl_gamepad_legacy_sticks` default 1,
  `cl_gamepad_invert_pitch` default 0). Both new cvars verified
  present in DLL via `strings cod4x_021.dll`.
- **Phase 3-C.2 (dead code):** new file
  `CoD4x_Client_pub/src/gamepad_move.c` (315 lines) with
  `gp_clamp_char`, `gp_apply_stick_deadzone`, `gp_populate_axes`,
  `gp_axis_value`, `gp_cl_gamepadmove`, `gp_cl_mousemove`. NO hook
  install. CMakeLists.txt updated.
- **Build (2026-05-27):** clean rebuild 68 s, exit 0, 0 new
  warnings. DLL size 2,624,512 -> 2,627,584 (+3,072 bytes = 6 PE
  alignment units). SHA256
  `B92F94BBE23ECC9AAC982C8F27DE079726816F2522A4963E03A0EE0A7F649B09`.
- **Smoke test (user):** 0 regression vs Phase 3-B. Buttons +
  triggers identical; sticks identical (Stage 3A CL_MouseEvent /
  arrow-key paths still in charge). Toggling
  `cl_gamepad_legacy_sticks 0` produced no behavior change,
  confirming the dispatcher is dead code (no hook installed).
- **Backup:** `cod4x_021.dll.phase3c1-pre` (Phase 3-B binary).

## Phase 3-C.4 -- resume brief for a fresh chat session

**Goal:** Install one hook + smoke-test the new analog usercmd path.

**Prerequisite state (already in master):**
- `gp_cl_mousemove(cmd)` is defined in `gamepad_move.c` and
  externally linked (callable from any TU), but currently
  uncalled.
- `cl_gamepad_legacy_sticks = 1` by default -- so even after the
  hook is installed, the new path stays dormant until the user
  flips the cvar to 0.
- iw3mp `0x463D70` is verified to be `CALL 0x463490` (cdecl),
  byte-identical to iw3sp_mod's `0x4402F7` (Stage 2 accepted).
  See `notes/stage3d-address-map.json` ->
  `IW3MP_CL_MOUSEMOVE_STUB_JMP`.

**Exactly four code changes needed:**

1. **Add hook install in `gamepad.c` `IN_StartupGamepads()`** --
   right after the cvars are registered:
   ```c
   GP_HOOK_JUMP(IW3MP_CL_MOUSEMOVE_STUB_JMP, gp_cl_mousemove);
   ```
   This is a single line. The helper macro is already in
   `gamepad_hooks.h`; the address is already in
   `gamepad_internal.h`. No new files.

2. **Add `#include "gamepad_hooks.h"`** at the top of
   `gamepad.c` if not already there. (Currently it includes
   `gamepad_internal.h` which transitively does NOT include
   the hooks header -- check before adding.)

3. **Build + deploy.** Backup as `cod4x_021.dll.phase3c4-pre`
   (preserves the .phase3c1-pre = Phase 3-B fallback intact).

4. **Smoke test sequence:**
   1. Game launches; title bar "Call of Duty 4 X".
   2. `\cl_gamepad 1` then `\cl_gamepad_legacy_sticks 1`
      (the default). Press a button on the pad to set inUse=true.
      Move the right stick: view should still drive through
      Stage 3A's CL_MouseEvent path -- new path INACTIVE.
   3. `\cl_gamepad_legacy_sticks 0`. Move the right stick: view
      should now drive through `cmd->yawmove/pitchmove`. Feel
      should be different (no acceleration curve yet -- raw
      linear). Movement (left stick) should be smooth analog
      instead of 8-way digital.
   4. `\cl_gamepad_legacy_sticks 1` again -> back to Stage 3A.
   5. Join a live CoD4x MP server with the new path active.
      Watch for kicks / desync / "client cmd time" errors.

**Risks to watch:**
- Re-entrancy: the engine's `CL_MouseMove` (0x463490) is now
  reachable both from our `gp_cl_mousemove` fallback AND from
  the patched-out call site at `0x463D70`. Our fallback uses a
  direct function-pointer call (`((cl_mousemove_fn)0x463490)`)
  which bypasses the patched callsite -- safe, no infinite
  recursion.
- The patched-out instruction is part of the engine's usercmd
  builder. If our hook target diverges in stack discipline, the
  engine will crash on return. cdecl was pre-flight-verified.
- MP server kicks if our writes to `cmd->forwardmove/rightmove`
  produce values outside what a keyboard would. The diagonal-
  normalize trick keeps the magnitude bounded by 127 (same as
  any +forward + +moveright combo). Should be transparent.

**Files to commit (Phase 3-C.4 single commit):**
- `src/gamepad.c` (+2 lines: include + hook install)
- A smoke-test session-status update in `mss32-proxy/notes/`.

**Reference docs (read in order if cold start):**
1. `D:\Cod4Project\PROJECT.md` (project overview)
2. `D:\Cod4Project\mss32-proxy\notes\stage3c-port-plan.md`
   (full Phase 3-C plan)
3. `D:\Cod4Project\mss32-proxy\notes\stage3d-address-map.json`
   (verified hook addresses)
4. This file (live session status)

**No need to re-read:** `gamepad.cpp` from iw3sp_mod-ref (already
ported), Stage 2 binary-diff artifacts (already consumed).

## Phase 3-C.4 build + deploy (2026-05-28)

**Design correction vs the resume brief:** the brief's one-line
`GP_HOOK_JUMP(0x463D70, gp_cl_mousemove)` was WRONG -- a raw JMP into a
`__cdecl` function at a CALL site crashes (no return address pushed, so
`cmd` is misread at `[esp+4]` and `ret` pops `cmd` as the return
target). iw3sp_mod doesn't do that either: its `HOOK_JUMP` target is the
`__declspec(naked)` trampoline `CL_MouseMove_Stub` (Gamepad.cpp:2079),
`call CL_MouseMove; jmp callsite+5`. Reviewer (second Claude) confirmed
the stack analysis on all three points. So 3-C.4 implements the
trampoline, not a direct jump.

**Address confirmations (pre-build):**
- `IW3MP_CL_MOUSEMOVE_STUB_JMP` = `0x463D70` (gamepad_internal.h:342 +
  address-map JSON line 73 -- the CALL site).
- `IW3MP_CL_MOUSEMOVE_FN` = `0x463490` (gamepad_internal.h:313 + JSON
  line 29 -- engine entry, stays clean for the dispatcher's fallback).
- trampoline return = `0x463D70 + 5 = 0x463D75` (no C macro for it; the
  `.asm` defines `CL_MOUSEMOVE_CALLSITE 0x463D70` and computes `+5`
  inline, so no bare magic number).

**Files written:**
- NEW `src/gamepad_stubs.asm` -- `gp_cl_mousemove_stub` naked trampoline
  (`call gp_cl_mousemove; mov eax, CL_MOUSEMOVE_CALLSITE+5; jmp eax`).
  `extern gp_cl_mousemove`, `global gp_cl_mousemove_stub`, no leading
  underscore (NASM `--prefix _` adds it). GPL-3/AGPL-3 header.

**Files modified:**
- `CMakeLists.txt` -- +1 line (`src/gamepad_stubs.asm` in the ASM list).
- `src/gamepad.c` -- `#include "gamepad_hooks.h"`, `extern void
  gp_cl_mousemove_stub(void);`, and a one-shot-guarded
  `GP_HOOK_JUMP(IW3MP_CL_MOUSEMOVE_STUB_JMP, gp_cl_mousemove_stub)` at
  the end of `IN_StartupGamepads()` (guard because IN_StartupGamepads
  re-runs on every `\in_restart` -- re-patch is idempotent but the guard
  is explicit).

**Build config change (forced):** prior cache was
`OFFICIAL_BUILD=ON`, but any CMakeLists edit forces a reconfigure, which
re-runs the `OFFICIAL` FetchContent of the PRIVATE
`git@github.com:callofduty4x/client-auth.git`. Offline this fails
(`Host key verification failed`) and the failed git-clone WIPED the
previously-fetched `_deps/client-auth-{src,build}`. Per user decision
(2026-05-28: "we work offline; the client maintainer handles anything
online; I test offline only") the build is now **`OFFICIAL_BUILD=OFF`**.
Implication: live/official-server auth is out of scope here; offline +
local/private MP only. To restore official builds later, re-fetch
client-auth with working SSH access to the callofduty4x org.

**Build (2026-05-28):** clean rebuild (`--clean-first`) via MinGW/CMake,
exit 0, 0 new warnings (only the pre-existing `_Direct3DCreate9@4`
stdcall-fixup). `gamepad_stubs.asm.obj` compiled; the `extern
gp_cl_mousemove_stub` resolved at link -> NASM symbol decoration
correct.
- New SHA256
  `C7380DD3600C8A43C6E6A970C1B4E3A4B6DE4AB70B7FC4FB68DDEA5D3C6770A9`.
- Size **2,627,584 B -- identical to the 3-C.1+.2 baseline** (trampoline
  + hook-install code fit in PE alignment slack). The byte-identical
  size also suggests the prior `OFFICIAL=ON` builds were effectively
  building without client-auth all along, so the offline OFF build is
  functionally close to what was being tested -- content differs (SHA
  changed) only marginally.
- **Backup:** `cod4x_021.dll.phase3c4-pre` = the 3-C.1+.2 binary
  (`B92F94BB...`, 2,627,584 B). The `.phase3c1-pre` (Phase 3-B
  fallback) remains intact.

**NOT committed** (per user). `cl_gamepad_legacy_sticks` default
**stays 1** -- the new analog path is dormant until the user flips it
to 0 in-game.

**Pending: user smoke-test** (the 5-step sequence in the resume brief):
launch -> `\cl_gamepad 1`, press a button (inUse=true) -> legacy=1 still
drives view via Stage 3A CL_MouseEvent -> `\cl_gamepad_legacy_sticks 0`
should switch to the new `cmd->yaw/pitch/forward/rightmove` path (raw
linear feel, smooth analog movement) -> flip back to 1 = Stage 3A ->
offline/local-MP only (no live official-server test this build).

## Stage 3B -- CoD4 Mod Tools (toolchain proven)

- SDK sparse-cloned to `D:\Cod4Project\tools\cod4-mod-tools\`
  (`bin` + `raw` + `zone_source`, 1,161 MB initial; +1.9 GB after
  `.iwi` extraction). NOT in any git repo.
- `linker_pc.exe` SHA256 `7a3bd700...524c31` -- verified across two
  mirrors (cod4mw + promod).
- `.iwi` images extracted from `game-test\main\iw_00..iw_13.iwd`
  (localized excluded) into `tools\cod4-mod-tools\raw\images\`:
  6,531 unique files, 1,923 MB.
- **Clean smoke build:** `linker_pc -language english ui_mp` (cwd =
  `bin\`) -> **0 errors**, `link...compress...save...done.` Produced
  `tools\cod4-mod-tools\zone\english\ui_mp.ff` 926,610 bytes
  SHA256 `9101C101...0AD230`.
- Toolchain end-to-end is **confirmed working on Windows 11**.
- Size note: rebuild is 2.2x the live `game-test\zone\english\ui_mp.ff`
  (420,146 bytes). Most likely the live file is a CoD4x-customized
  stripped variant; ours is the full IW v1.4 retail rebuild. Not a
  toolchain failure. Implication for deployment is open.
- `game-test\zone\english\ui_mp.ff` (the live game UI) is UNTOUCHED.

## Other repos (baseline -- not in active flux)

| Repo | Last commit | Tag |
|---|---|---|
| CoD4x-launcher | `def459c` | `baseline-working` |
| mss32-proxy | `1d783b1` (+ uncommitted notes) | `baseline-working` |
| CoD4x_Client_pub | `0081a30` | `baseline-working` on `880c04f` |
