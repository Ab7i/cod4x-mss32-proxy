# Session status -- living file

Updated after every execution step. Purpose: never again confuse
"code presented for review" with "code actually built & deployed".

_Last updated: 2026-05-27, after Phase 3-C.1+3-C.2 verified -- Phase 3-C.4 (hook install) pending._

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
