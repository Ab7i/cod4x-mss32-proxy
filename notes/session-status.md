# Session status -- living file

Updated after every execution step. Purpose: never again confuse
"code presented for review" with "code actually built & deployed".

_Last updated: 2026-05-26, after Stage 3-A code (infrastructure only -- not yet built)._

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
