# Stage 3 -- Port `Gamepad.cpp` from `iw3sp_mod` into `CoD4x_Client_pub`

_Written 2026-05-26 after Stage 2 finish. **Reading + planning only.** No
code has been written for Stage 3 yet._

---

## 0. TL;DR

- Source: `tools/iw3sp_mod-ref/src/Components/Modules/Gamepad.{cpp,hpp}`
  — 2,355 + 212 = **2,567 lines, 90 member functions, 21 hook sites**.
- Target: `CoD4x_Client_pub/src/gamepad.c` (currently 461 lines, Stage
  3A) plus new C files to be created.
- Address map: 17/19 hot iw3mp addresses are byte-trustworthy (Stage 2
  finish). 2 hooks (`Player_UseEntity`, `UI_RefreshStub`) deferred to
  Stage 3.5 polish.
- Estimated effort: **~6 working sessions** broken into Phases A–E
  below. Phase A unblocks everything else; B alone gives a "first
  playable controller build."

## 1. Anatomy of `Gamepad.cpp` (line ranges, by purpose)

| Lines | Section | Lines count | What it does |
|---|---|---:|---|
| 1–189 | Data tables | 189 | Button/stick maps, key-name tables (Xenon/PS3 art tags) |
| 190–291 | `GetButtonLayout` | 102 | Picks Xbox vs PS HUD glyph names from `gpad_style` |
| 292–349 | Hints + state-changed | 58 | `UpdateGamepadHint`, `UpdateTheButtonAHint`, `HasGamepadStateChanged` |
| 351–826 | **AimAssist_*** | **476** | The heart of aim assist: pitch/yaw mapping, slowdown, lock-on, auto-aim, melee. 14 funcs. |
| 827–892 | Misc helpers | 66 | `CG_ShouldUpdateViewAngles`, `CL_GamepadAxisValue`, `ClampChar` |
| 894–971 | **Movement path** | 78 | `CL_GamepadMove` (apply pad → `usercmd_s`), `CL_MouseMove` wrapper |
| 972–987 | Rumble | 16 | `Vibrate` via XInput |
| 988–1095 | Bindings (forward) | 108 | `GetGamePadCommand`, `Key_GetCommandAssignmentInternal` + **naked stub** |
| 1096–1140 | Key/Mouse event hooks | 45 | `Key_SetBinding_Hk`, `CL_KeyEvent_Hk`, `HideSystemCursor`, `IsGamePadInUse`, `OnMouseMove`, `UI_RefreshViewport_Hk` |
| 1141–1230 | **5 naked-asm stubs** | 90 | `GetLocalizedKeyName_Stub{,02}`, `UI_RefreshStub`, `UI_MouseEventStub` |
| 1232–1394 | Button-event pipeline | 163 | `GPad_Check`, `GPad_RefreshAll`, `CL_CheckForIgnoreDueToRepeat`, `CL_GamepadButtonEvent{,ForPort}` |
| 1395–1675 | Stick reading + bind axis | 281 | `GPad_ConvertStickToFloat`, `Gamepad_ShouldUse`, `Key_IsValidGamePadChar`, `CL_GamepadResetMenuScrollTime`, `CL_GamepadGenerateAPad`, `CL_GamepadEvent`, `UI_GamepadKeyEvent`, `GPad_GetStick/Button`, `GPad_IsButtonPressed/Released` |
| 1677–1820 | **Polling core** | 144 | `GPad_UpdateSticksDown`, `GPad_UpdateSticks`, `GPad_UpdateDigitals`, `GPad_UpdateAnalogs`, `GPad_UpdateAll` |
| 1820–1897 | Cheat-code | 78 | The 10-button "IW Code" easter egg |
| 1898–1959 | **`IN_Frame_Hk` + `IN_GamePadsMove`** | 62 | The hook installed at iw3mp 0x576193 — root of XInput polling per frame |
| 1960–2070 | `Key_WriteBindings_Hk` + **5 naked stubs** | 111 | Persist bindings to `config_mp.cfg` |
| 2070–2200 | `Gamepad_BindAxis`, `String→Enum`, `CreateKeyNameMap`, `Axis_Bind_f` | 130 | Console `bindaxis` command + key-name table merge |
| 2200–2238 | `Bind_GP_*Configs_f`, `SetAimAssist_f` | 38 | Config-file exec helpers |
| 2238–2351 | **Component constructor** | 114 | `Dvar_Register*` (29 dvars), all 21 hook installs, command registrations |
| 2353–2356 | Dtor | 3 | empty |

### 1a. The 12 naked-assembly stubs

These are trampolines that resume original engine code after the C/C++
hook function runs. They CANNOT be ported as-is — MSVC `__declspec(naked)`
+ inline `__asm { ... }` doesn't exist in GCC/MinGW. **All 12 must be
rewritten** as either:
- A separate `.s` (AT&T) or `.asm` file processed by the existing
  `callbacks.asm` toolchain (preferred — there is already
  `client_callbacks.asm` next to `callbacks.asm`), or
- GCC `__asm__ __volatile__("...")` blocks inside a wrapper function
  (works for ~5-instruction tails but ugly for the larger stubs).

Stubs inventory:
1. `Key_GetCommandAssignmentInternal_Stub` — 1056
2. `GetLocalizedKeyName_Stub` — 1144
3. `GetLocalizedKeyName_Stub02` — 1163
4. `UI_RefreshStub` — 1182
5. `UI_MouseEventStub` — 1201
6. `Key_WriteBindings_Stub` — 1995
7. `Com_WriteConfiguration_Modified_Stub` — 2014
8. `Key_SetBinding_stub01` — 2039
9. `Key_SetBinding_stub02` — 2052
10. `Key_SetBinding_stub03` — 2065
11. `Player_UseEntity_Stub` — referenced but its body is elsewhere; check
12. `CL_MouseMove_Stub`, `CL_MouseEvent_Stub` — referenced

## 2. Dependencies — every symbol `Gamepad.cpp` reaches outside itself

### 2a. C++ stdlib (used in the source, must be replaced or kept inside .cpp)
- `<XInput.h>` ✅ already linked by `CoD4x_Client_pub/CMakeLists.txt`
  (`xinput9_1_0`).
- `<functional>` (`std::function`) — used by `Utils::Hook::Call<T>` for
  the typed call-original wrapper, and by `GamePadCheat::unlockFunc`.
- `std::numeric_limits<int>::max()` — single use in dvar registration
  (line 2260).
- `std::extent_v<decltype(...)>` — line 154-156 sizing the combined
  key-name arrays.
- `std::string` — minor (`empty()`, ctor from `const char*`).

### 2b. iw3sp_mod-only utility headers (must be replaced by CoD4x equivalents)

| iw3sp_mod symbol | CoD4x equivalent | Notes |
|---|---|---|
| `Utils::Hook(addr, fn, HOOK_JUMP).install()->quick()` | `SetJump((DWORD)addr, fn)` in `src/sys_patch.h` | Already there. |
| `Utils::Hook(addr, fn, HOOK_CALL).install()->quick()` | `SetCall((DWORD)addr, fn)` in `src/sys_patch.h` | Already there. |
| `Utils::Hook::Nop(addr, n)` | `Patch_Memset((void*)addr, 0x90, n)` | Already in `sys_patch.c`. |
| `Utils::Hook::Set<T>(addr, value)` | Direct write with `VirtualProtect` wrapper; CoD4x has `WriteSymbol(DWORD addr, void* symbol)` for ptr writes — needs **a tiny `Patch_SetT(addr, &val, sizeof val)`** helper to be added. |
| `Utils::Hook::Call<T>(addr)(args...)` | Plain function-pointer cast: `((T)addr)(args)` — direct C is simpler than the C++ wrapper. |
| `Utils::String::VA(fmt, ...)` | Already exists in CoD4x as `va(fmt, ...)` in `q_shared.c`. |
| `Dvars::Register::Dvar_Register{Bool,Float,Int,String}` | `Cvar_Register{Bool,Float,Int,String}` in `cvar.c` (already used by current `gamepad.c`). Signatures differ slightly (flag values differ). |
| `Dvars::Functions::Dvar_FindVar` | `Cvar_FindVar` in `cvar.c`. |
| `Command::Add(name, fn)` | `Cmd_AddCommand(name, fn)` in `cmd.c`. |
| `Command::Execute(str, sync)` | `Cbuf_AddText(str)` (already used everywhere in CoD4x). |
| `Events::OnDvarInit([]{ ... })` | No direct equivalent — call our register-dvars function from `gamepad_Init()` once at startup. |
| `UIScript::Add("gamepadFirstExecIfEnabled", fn)` | Defer — UI integration is Phase E. |

### 2c. `Game::` namespace — engine structs, enums, raw function pointers

Total: **141 unique `Game::` symbols** referenced. They split into 4 buckets:

1. **Pure constants/enums** (need a translation header `game_gamepad.h`
   we create in CoD4x): `GPAD_*` (24 values), `K_BUTTON_*` /
   `K_DPAD_*` / `K_APAD_*` (~25 keynums), `GamePadButtonEvent`,
   `GamePadStick`, `GamepadPhysicalAxis`, `GamepadVirtualAxis`,
   `GamepadMapping`, `GPAD_PHYSAXIS_COUNT`, `GPAD_VIRTAXIS_COUNT`,
   `MAX_GPAD_COUNT=1`, etc. **All compile-time constants** — just copy
   the values across (Structs.hpp lines 5716-5870).
2. **Plain structs** (POD, also pure data): `keyname_t`,
   `ButtonToCodeMap_t`, `StickToCodeMap_t`, `GpadAxesGlob`,
   `GamePadCheat` (inside Gamepad), `usercmd_s`. `usercmd_s` already
   exists in CoD4x (verify exact layout against iw3mp at 0x463490
   callsite — Stage 2 byte-verify confirmed it). Others are gamepad-
   specific and we own them.
3. **Aim-assist structs** (heavy): `AimInput` (Structs.hpp L6029),
   `AimOutput` (L6045), `AimAssistGlobals` (L6077),
   `AimScreenTarget`. These are **engine** structs read by hot engine
   code in iw3mp at runtime. Memory layout MUST match what iw3mp
   expects. Need to verify each field offset against iw3mp before
   trusting them. Aim assist is Phase D — full struct verification
   happens then.
4. **Engine functions called by address** — `Game::Cbuf_AddText`,
   `Game::Cbuf_InsertText`, `Game::Com_Printf`, `Game::FS_Printf`,
   `Game::Key_IsCatcherActive`, `Game::Key_SetBinding`,
   `Game::Key_WriteBindings`, `Game::UI_SetActiveMenu`,
   `Game::UI_KeyEvent`, `Game::R_Cinematic_IsStarted`,
   `Game::Sys_MilliSeconds`, `Game::DiffTrackAngle`,
   `Game::AngleSubtract`, `Game::Vec2Normalize`, `Game::BG_WeaponAmmo`,
   `Game::GraphFloat_GetValue`, `Game::DB_IsZoneLoaded`,
   `Game::HasLoadedMod`, `Game::CL_StopLogoOrCinematic`,
   `Game::AimAssist_ApplyAutoMelee`, `Game::AimAssist_ApplyMeleeCharge`,
   `Game::AimAssist_UpdateAdsLerp`,
   `Game::AimAssist_UpdateTweakables`.
   - **About half are already exported by CoD4x** (`Com_Printf`,
     `Cbuf_AddText`, `Cvar_FindVar`, etc. — confirm by grep in
     `CoD4x_Client_pub/src/`).
   - The other half are called by **raw iw3mp address** in iw3sp_mod
     (e.g. `Functions.hpp` declares `Sys_MilliSeconds = 0x...`). For
     CoD4x we either (a) declare a function pointer initialized to the
     iw3mp address, or (b) find that CoD4x already wraps it. Phase A
     produces the table.

### 2d. Globals referenced
- `Game::uiInfo` — `uiInfo_s*` global
- `Game::uiInfo->uiDC.localClientNum` — single-int read
- `Dvars::*` (29 dvar pointers, declared in `Modules/Dvars.hpp` of the
  ref — we register them into our own static array of `cvar_t*`)

## 3. Build-system gap (iw3sp_mod ↔ CoD4x)

| Concern | iw3sp_mod | CoD4x_Client_pub | Mitigation |
|---|---|---|---|
| Generator | premake5 → MSVS sln | CMake → MinGW (gcc/g++) | New code lives in CoD4x repo only. Reference repo stays read-only. |
| Language | C++20 (`std::ranges`, `std::extent_v`, generic lambdas) | Mostly C99 with some C++ glue files (`*.cpp` allowed by CMakeLists) | **All ported code goes in `.c`** to match the rest of `src/gamepad.c`. Drop C++ features (templates → macros or duplicated typed funcs; `std::function` → plain fn pointers). |
| Inline asm | MSVC `__declspec(naked)` + `__asm{}` (Intel) | GCC inline `__asm__` (AT&T) or external `.asm` via the existing `nasm` build step (cf. `callbacks.asm`, `client_callbacks.asm`) | Rewrite all 12 naked stubs as AT&T snippets or, better, append to `client_callbacks.asm`. |
| Strings/format | `Utils::String::VA` | `va()` from `q_shared.c` | 1:1 rename. |
| Hook lib | `Utils::Hook` (MinHook + custom 5-byte rewriter) | `SetCall/SetJump/Patch_Memset` in `sys_patch.c` | We only need 5-byte `JMP rel32`/`CALL rel32` patches, which is exactly what CoD4x already provides. |
| Dvars | `cvar_t*` with iw3sp_mod `flags` enum (`saved=1`, `none=0`) | `cvar_t*` with CoD4x `CVAR_ARCHIVE`, `CVAR_INFO`, etc. flag bits | Translation table in our new code. |
| Loader pattern | Each Component has ctor that auto-runs at DllMain | Single `gamepad_Init()` we call from `Sys_LoadGame` (or wherever the existing `IN_StartupGamepads` is called) | Manual init call. |
| Localization | `LocalizedStrings` table loader (mod-tool baked) | Already wired via `ui_mp.ff`/fastfile pipeline (Stage 3B toolchain proven) | Phase E only. |

**Net cost of language gap:** ~10–15 % of the ported lines change
shape (templates expanded, `std::function` removed, naked stubs
externalized). The actual *logic* lines are nearly 1:1.

## 4. Phased port plan

Each phase is a single session ending in a build + deploy with the
session-status.md update. **No phase moves forward until the previous
deployment is verified in-game.**

### Phase 3-A — Infrastructure & address map (no game features yet)

**Goal:** Lay the cables. No new gameplay behavior.

1. Create `src/gamepad_addrs.h` — every iw3mp address from
   `stage3d-address-map.json` as a `#define` constant.
2. Create `src/gamepad_engine.h` — function-pointer typedefs + table of
   engine functions called by address (Cbuf_AddText, Key_SetBinding,
   AimAssist_ApplyAutoMelee, …). Each entry: `typedef void
   (*Cbuf_AddText_t)(int, const char*); static const Cbuf_AddText_t
   Game_Cbuf_AddText = (Cbuf_AddText_t)0x...;`
3. Create `src/gamepad_types.h` — `GPAD_*` enums, `keyname_t`,
   `ButtonToCodeMap_t`, `StickToCodeMap_t`, `GpadAxesGlob`, the gamepad
   internal `GamePad`, `GamePadGlobals`, `ButtonMappings` structs.
   **No** aim-assist structs yet (those land in Phase D after byte
   verification).
4. Add `Patch_SetPtr(DWORD addr, void* value)` helper to `sys_patch.c`
   (= `Utils::Hook::Set<T*>` analog) — 8 lines, wraps `VirtualProtect`.
5. Verify the project still builds with the new headers included but
   no behavior change.

**Touches:** new files only + `CMakeLists.txt`. No hooks installed,
no behavior change. **Deploy:** verify dll loads cleanly, no
regressions.

**Exit gate:** `cod4x_021.dll` still works exactly like Stage 3A;
hashes show only the addition of new symbols.

### Phase 3-B — Core polling + button events (first playable controller build)

**Goal:** Hijack `IN_Frame` and pipe XInput state into the engine's
key-event path. Movement and look DO NOT use new code yet — they keep
running through current Stage 3A code so the user has continuity.

Port these functions (source lines in parens):
- `GPad_Check` (1232), `GPad_RefreshAll` (1249), `GPad_UpdateAll`
  (1796), `GPad_UpdateDigitals` (1745), `GPad_UpdateAnalogs` (1770),
  `GPad_UpdateSticks` (1712), `GPad_UpdateSticksDown` (1677).
- `GPad_GetStick` (1577), `GPad_GetButton` (1586),
  `GPad_IsButtonPressed/Released/RequiresUpdates` (1610–1675).
- `CL_GamepadButtonEvent{,ForPort}` (1295, 1385),
  `CL_CheckForIgnoreDueToRepeat` (1261).
- `IN_GamePadsMove` (1898), `IN_Frame_Hk` (1954) — install at iw3mp
  `0x576193`.
- `Vibrate` (972).

Install hooks:
- `SetCall(0x576193, IN_Frame_Hk)`
- (deferred to 3-C) `SetJump(0x463D70, CL_MouseMove_Stub)` —
  movement injection
- (deferred to 3-C) `SetJump(0x576??? CL_MouseEvent_Stub)` — once we
  resolve the 0x5947A8 iw3mp twin

**Exit gate:** Console prints `[gamepad] IN_Frame_Hk` once per frame
when `gpad_enabled 1`. Buttons fire via engine `Key_Event` path
(verify by `bind BUTTON_A "say hello"` then press A).

### Phase 3-C — Movement + look path replacement

**Goal:** Replace the Stage 3A "synthesize arrow-key + mouse events"
code with iw3sp_mod's proper `CL_GamepadMove` → `usercmd_s` path.

Port:
- `CL_GamepadAxisValue` (855), `ClampChar` (889), `CL_GamepadMove`
  (894), `CL_MouseMove` (958), `CL_MouseMove_Stub` (naked-2330),
  `CL_MouseEvent_Stub` (naked-2311–2312), `CL_MouseEvent_Hk` (1077),
  `OnMouseMove` (1125).
- `CL_GamepadEvent` (1537), `CL_GamepadGenerateAPad` (1502),
  `CL_GamepadResetMenuScrollTime` (1480), `Key_IsValidGamePadChar`
  (1473), `UI_GamepadKeyEvent` (1555), `Gamepad_ShouldUse` (1438).
- `GPad_ConvertStickToFloat` (1402).

Install hooks:
- `SetJump(0x463D70, CL_MouseMove_Stub)`
- `SetJump(0x???????, CL_MouseEvent_Stub)` — pending 0x5947A8 → iw3mp
  twin (Stage 3.5)
- `SetCall(0x4FDCBF, CL_KeyEvent_Hk)` — mark controller unused when
  keyboard pressed

**Exit gate:** Right stick looks via `usercmd_s` viewangles (no more
fake mouse-events). Left stick smoothly drives `forwardmove`/
`rightmove`. Stage 3A debug logging removed.

### Phase 3-D — Aim assist

**Goal:** Port the 476-line aim-assist subsystem.

**Pre-flight:** Byte-verify the layout of `AimInput`, `AimOutput`,
`AimAssistGlobals` against iw3mp. The aim-assist engine helpers
(`AimAssist_ApplyAutoMelee`, `AimAssist_UpdateAdsLerp`,
`AimAssist_UpdateTweakables`) live at known iw3sp_mod addresses; need
to find their iw3mp twins (likely already in Stage 2 results — check
the VT session for any aim_* prefixed function names).

Port all `AimAssist_*` functions (351–826) plus
`AimAssist_UpdateGamePadInput` (827) and `CG_ShouldUpdateViewAngles`
(850).

**No new hook install** at this phase — aim assist runs as a
sub-routine called from `CL_GamepadMove` (already ported in 3-C).

**Exit gate:** Toggle `gpad_aimassist 1` in-game, verify slowdown
near a moving target.

### Phase 3-E — Bindings, configs, key-name table, UI commands

**Goal:** Persistent bindings, console commands, button-glyph names.

Port:
- `CreateKeyNameMap` (~2100), `GetLocalizedKeyNameMap` (1139),
  `GetLocalizedKeyName_Stub{,02}` (naked-1144, 1163).
- `Gamepad_BindAxis`, `String→Enum` helpers (`StringToPhysicalAxis`,
  `StringToVirtualAxis`, `StringToGamePadMapping`),
  `Axis_Bind_f` / `Axis_Unbindall_f` (2070+).
- `Key_SetBinding_Hk` (1096), `Key_SetBinding_stub01-03`
  (naked-2039+), `Key_GetCommandAssignmentInternal{,_Stub}`
  (1003, naked-1056).
- `Gamepad_WriteBindings` (1960), `Key_WriteBindings_Hk` (1985),
  `Key_WriteBindings_Stub` (naked-1995),
  `Com_WriteConfiguration_Modified_Stub` (naked-2014).
- `Bind_GP_SticksConfigs_f`, `Bind_GP_ButtonsConfigs_f`,
  `SetAimAssist_f`, `GetButtonLayout`, `GetGamePadCommand`.
- All 29 dvar registrations.
- Cheat code (1820–1897) — optional, but the test of bindings.

Install hooks (the remaining 13 from the address map):
`0x4676F0+0x8D`, `+0x95`, `0x4677C0+0x77`, `+0x6F`, `0x475DC0+0x91`,
`0x4678E0`, `0x4FDCBF`, `0x4FFB0F`, `0x5529B8`, `0x5529CB`,
`0x5529E3`, `0x423890?` (reload-hint pending Stage 3.5 offset
search).

**Exit gate:** `bind BUTTON_A "+attack"` writes the line correctly
into `config_mp.cfg`, persists across restart, glyphs render in the
console autocomplete.

### Phase 3.5 — Polish (deferred from Stage 2)

- Find correct offset inside iw3mp `0x4237B0` for the reload-hint
  patch (byte-pattern scan around the `push 0x680020; call;
  mov ecx,[mem]; jmp` sequence). Then install
  `Patch_SetPtr(0x4237B0+X, "PLATFORM_RELOAD")` toggle.
- Resolve iw3mp twin for `0x5947A8` (CL_MouseEvent_Stub). Strategy:
  locate iw3mp twin of `0x43F920` (callee), then walk its callers in
  iw3mp to find the equivalent stub block.
- Resolve `Player_UseEntity` iw3mp twin (`0x4F92A0` in src). Defer
  manual decomp comparison — non-blocking.
- Resolve `UI_RefreshStub` iw3mp twin (`0x565360` in src). Likely
  inlined into a Refresh function; manual decomp.

### Phase 4 — Menu integration (separate from Stage 3)

UI/menu integration uses the proven `linker_pc.exe` toolchain from
Stage 3B. Not part of Stage 3 — a separate stage with its own plan.

## 5. Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Aim-assist struct layout differs SP→MP | **medium** | Byte-verify each field offset in Phase D pre-flight |
| `usercmd_s` MP layout differs from `usercmd_s` SP | **low** | Stage 2 confirmed via VT byte-identity of `0x463490` — same callsite, same args |
| Naked-stub rewrites accidentally trash a register the engine relies on | **medium** | Each stub tested in isolation with `gpad_debug 1` printf trace |
| CoD4x already registers a cvar that collides with an iw3sp_mod one (e.g. `gpad_enabled`) | **low** | Pre-search `CoD4x_Client_pub/src/` for each of the 29 names; rename ours with `cl_` prefix where collision exists (already do this for the 7 Stage 3A cvars) |
| `IN_Frame_Hk` collides with the existing CoD4x `IN_StartupGamepads` integration in `win_input.c` | **medium** | Stage 3A's `IN_GamepadsMove` call from `win_input.c` will be replaced by the new `IN_Frame_Hk` patch at `0x576193`; keep the existing C function as the body until 3-B is verified, then move logic into the hook |
| Mod-tools changes UI bindings before menus exist | **low** | Phase 4 strictly after Phase 3 done |
| **Engine fn uses `__usercall` (register args) -- a `__cdecl` hook clobbers them and crashes inside the engine** | **HIGH / CONFIRMED** | **PROVEN in Phase 3-C.4:** iw3mp `CL_MouseMove` takes its first arg (client index) in EAX. Our `__cdecl` hook clobbered EAX, faulting at 0x4635CB only on the movement path. Fix: `__attribute__((regparm(1)))`. **MANDATORY for every future hook: disassemble call site + callee entry, match the calling convention (cdecl / regparm(N) / stdcall / naked asm), forward with the same convention. See stage3c-port-plan.md section 14.** |
| Self-unprotect: CoD4x `SetCall`/`SetJump` are raw (no VirtualProtect) | **CONFIRMED** | Hooks install from `IN_StartupGamepads`, outside `Patch_MainModule`'s unprotect window -> raw write faults. Use `Patch_SetCall`/`Patch_SetJump` (added Phase 3-C.4); GP_HOOK_* macros already route through them. |

## 6. Time estimate

| Phase | Estimated sessions | Why |
|---|---:|---|
| 3-A Infrastructure | 1 | Headers + helper; mostly typing |
| 3-B Polling + buttons | 1–2 | Hook install + first verifiable in-game test |
| 3-C Movement + look | 1 | Self-contained replacement of Stage 3A |
| 3-D Aim assist | 1–2 | Largest in lines; needs struct verify |
| 3-E Bindings + configs | 1 | 13 hook installs + 12 naked-stub rewrites |
| 3.5 Polish | 0.5–1 | Three deferred Stage-2 items |
| **Total** | **~6 sessions** | |

## 7. Decisions resolved by user (2026-05-26)

1. **`gpad_aimassist` default = OFF.** User opt-in only. Reasoning:
   MP players are split; some servers may reject clients with aim
   assist; OFF means the user enables it consciously. Persisted to
   `config_mp.cfg` via `CVAR_ARCHIVE`.
2. **Cheat code (IW Code easter egg) = SKIPPED.** SP-only feature;
   78 lines saved; lower port complexity. See "Skipped from iw3sp_mod"
   section below.
3. **Discord rich-presence = DEFERRED to Phase 4.** Logged separately
   in `notes/next-phase-goals.md`.
4. **GPL-3 attribution header (mandatory in every new `gamepad_*.c`
   file):**

   ```c
   /*
    * gamepad_X.c -- [description]
    *
    * Derived from iw3sp_mod by JerryALT
    *   https://gitea.com/JerryALT/iw3sp_mod
    *   Licensed under GPL-3.0
    *
    * Ported to CoD4x multiplayer by Ab7i
    *   https://github.com/Ab7i
    *   Licensed under AGPL-3.0 (compatible with GPL-3.0)
    *
    * iw3sp_mod itself is derived from IW4x Client.
    * Original Gamepad subsystem code patterns (c) IW4x team.
    */
   ```

## 8. Skipped from iw3sp_mod (will not be ported)

- **IW Code easter-egg cheat** (Gamepad.cpp lines 1820-1897, plus the
  `GamePadCheat` struct in Gamepad.hpp lines 7-17). 78 source lines.
  SP-only flavor; not meaningful in competitive MP. May revisit as an
  optional Stage 5+ item if community asks for it.

## 9. Stage 3-A execution log (2026-05-26)

Files created in `CoD4x_Client_pub/src/`:

| Path | Lines | Purpose |
|---|---:|---|
| `gamepad_internal.h` | 211 | Counts, GP_* masks, gp_button_e/gp_stick_e/event enums, engine-ABI GAME_K_* keynums, POD structs (`gp_keyname_t`, `gp_button_to_code_t`, `gp_stick_to_code_t`, `gp_axes_glob_t`), internal state structs (`gp_state_t`, `gp_globals_t`, `gp_button_mappings_t`), and the 14 resolved iw3mp hook-site/function `#define`s. |
| `gamepad_hooks.h` | 51 | 4 macros: `GP_HOOK_JUMP`, `GP_HOOK_CALL`, `GP_HOOK_SET_PTR`, `GP_HOOK_NOP`. Wraps existing `SetJump`/`SetCall`/`Patch_SetPtr`/`Patch_Memset`. |

Files modified:

| Path | Change |
|---|---|
| `sys_patch.h` | Added prototype `void Patch_SetPtr(DWORD addr, void* value);` (8-line doc comment included). |
| `sys_patch.c` | Added `Patch_SetPtr` body (~15 lines). Self-unprotects via `VirtualProtect`, writes pointer, restores protection, flushes I-cache. Matches iw3sp_mod `Utils::Hook::Set<T*>` semantics exactly. |

Files **not** touched (per user instruction):

- `src/gamepad.c` (Stage 3A code -- stays untouched until Phase 3-B)
- `src/gamepad.h`           (current `IN_StartupGamepads` / `IN_GamepadsMove` API still valid)
- `src/win_input.c`          (callers of the Stage 3A API stay as-is)
- `CMakeLists.txt`           (no new translation units yet; only headers)

Sanity checks already done (no build run yet):

- All `GAME_K_*` values cross-referenced against iw3sp_mod
  `Game/Structs.hpp` L5578-5712 (engine-ABI keynum_t).
- All `GP_*` masks/counts cross-referenced against L5716-5870.
- All 14 hook addresses lifted from `stage3d-address-map.json` v3
  (`accepted` rows only -- the 17 hooks minus the 3 stragglers).
- `Patch_SetPtr` uses `PAGE_EXECUTE_READWRITE` to match the iw3mp
  .text section's executable bit (the existing CoD4x `Sys_PatchSection`
  uses `PAGE_READWRITE` -- our self-unprotect path differs because we
  can't assume the caller bracketed us).
- GPL-3 attribution header included in `gamepad_internal.h` and
  `gamepad_hooks.h` (the user-approved boilerplate).

## 10. Next step -- Phase 3-B (Polling + buttons)

**Goal:** First playable controller build. XInput state piped through
the engine's native `Key_Event` path so any button can be bound via
the console.

**Files to create in Phase 3-B:**
- `src/gamepad_engine.h` -- function-pointer typedefs for engine
  functions we will call by raw address (Cbuf_AddText, Key_SetBinding,
  Sys_MilliSeconds, ...). Only entries actually used in 3-B are added.
- `src/gamepad_poll.c` -- ports `GPad_UpdateAll` / `GPad_UpdateSticks`
  / `GPad_UpdateDigitals` / `GPad_UpdateAnalogs` / `GPad_RefreshAll`
  / `IN_GamePadsMove` / `IN_Frame_Hk` from Gamepad.cpp.
- `src/gamepad_buttons.c` -- ports `CL_GamepadButtonEvent{,ForPort}`,
  `CL_CheckForIgnoreDueToRepeat`, `GPad_IsButtonPressed/Released`.

**Single hook installed in 3-B:** `GP_HOOK_CALL(IW3MP_IN_FRAME_CALL,
IN_Frame_Hk)` at iw3mp `0x576193`.

**Exit gate:** in-game, `gpad_enabled 1` + `gpad_debug 1` should log
once per frame; `bind BUTTON_A "say hello"` then pressing A should
route through the engine and print "hello" in chat.

_Stage 3-A complete. Awaiting user review of the four new/changed
files, then user-driven build + smoke test, then green light for
Phase 3-B._

---
_Status when this file was written: Stage 2 done, Stage 3 not started._
_Next step: user reviews this plan; on green light, Phase 3-A begins
in the next session._
