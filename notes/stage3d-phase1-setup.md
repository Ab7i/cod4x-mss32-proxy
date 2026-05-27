# Stage 3D / Phase 1 -- RE setup (read-only)

Goal: prepare the binary-diff workflow that will let us port the
iw3sp_mod gamepad subsystem from `iw3sp.exe` to `iw3mp.exe`. This file
gathers everything Phase 2 will execute against.

## 1. Hook inventory from `Gamepad.cpp`

Grep of `tools\iw3sp_mod-ref\src\Components\Modules\Gamepad.cpp`. Three
call shapes:
- `Utils::Hook(addr, fn, HOOK_*).install()` -- install a detour.
- `Utils::Hook::Set<T>(addr, value)` -- overwrite a 4-byte slot inline
  (typically an immediate operand or a global pointer).
- `Utils::Hook::Call<sig>(addr)(...)` -- call the original engine
  function directly via its address (no install).
- `Utils::Hook::Nop(addr, len)` -- erase bytes before installing a
  jump (skip the original short instruction the jump would clobber).

### 1.a Active hook sites (19 unique addresses)

| # | iw3sp addr | Kind | iw3sp_mod symbol | Purpose |
|---|---|---|---|---|
| 1 | `0x40A0B0` | Set<const char*> | `"..._RELOAD_GAMEPAD"` / `"PLATFORM_RELOAD"` | Toggle reload-hint string between pad and KB |
| 2 | `0x43FA90` | Call<void(usercmd_s*)> | `CL_MouseMove` | Engine fn called from our `CL_GamepadMove` |
| 3 | `0x443D60` | Call<void(int,int,int,unsigned)> | `CL_KeyEvent` | Engine fn called from our `CL_KeyEvent_Hk` |
| 4 | `0x44367D` | Set<keyname_t*> | `combinedKeyNames` | Pointer load for keyName table (adds BUTTON_*) |
| 5 | `0x443685` | Set<keyname_t*> | `combinedKeyNames` | Same, second load site |
| 6 | `0x443737` | Set<keyname_t*> | `combinedKeyNames` | Same, third load site |
| 7 | `0x44372F` | HOOK_CALL | `GetLocalizedKeyName_Stub` | Emit BUTTON_* names from localized-name lookup |
| 8 | `0x447E01` | HOOK_CALL | `GetLocalizedKeyName_Stub02` | Second GetLocalizedKeyName hook site |
| 9 | `0x4402F7` | HOOK_JUMP | `CL_MouseMove_Stub` | Replace mouse-move path with gamepad-aware |
| 10 | `0x4437E0` | HOOK_JUMP | `Key_GetCommandAssignmentInternal_Stub` | Extend Key_GetCommandAssignment for pad keys |
| 11 | `0x4F92D7` | HOOK_JUMP | `Player_UseEntity_Stub` | Context-sensitive +activate for pad |
| 12 | `0x533B8F` | HOOK_CALL | `CL_KeyEvent_Hk` | Gamepad-aware key-event router |
| 13 | `0x53537F` | HOOK_JUMP | `Key_WriteBindings_Stub` | Save gamepad bindings to config |
| 14 | `0x5653CA` | HOOK_JUMP | `UI_RefreshStub` | UI refresh hook (menu pad input) |
| 15 | `0x56F728` | HOOK_JUMP | `Key_SetBinding_stub01` | Key_SetBinding handles BUTTON_* (#1) |
| 16 | `0x56F73B` | HOOK_JUMP | `Key_SetBinding_stub02` | Key_SetBinding (#2) |
| 17 | `0x56F753` | HOOK_JUMP | `Key_SetBinding_stub03` | Key_SetBinding (#3) |
| 18 | `0x594913` | HOOK_CALL | `IN_Frame_Hk` | Per-frame input hook (root of XInput poll) |
| 19 | `0x5947A8` | HOOK_JUMP + Nop(9) | `CL_MouseEvent_Stub` | Replace CL_MouseEvent for pad routing |

### 1.b Commented-out (inactive) -- documented for completeness

| iw3sp addr | Kind | Symbol | Notes |
|---|---|---|---|
| `0x53542C` | HOOK_JUMP + Nop(7) | `Com_WriteConfiguration_Modified_Stub` | Disabled in iw3sp_mod; revisit if config-write breaks |
| `0x43F921` | HOOK_JUMP + Nop(10) | `UI_MouseEventStub` | Disabled in iw3sp_mod; revisit if menu mouse breaks |

Structured form: `notes\stage3d-address-map.json` (machine-readable
tracking; each row has `iw3sp_addr`, `iw3mp_addr` (to fill), `kind`,
`purpose`, `status`).

## 2. `Game/Functions.hpp` -- the wider engine address map

`src/Game/Functions.hpp` (272 lines) and `Functions.cpp` (1,360
lines) declare/initialize the engine *helper* addresses that the
iw3sp_mod components reuse. These are NOT hook sites -- they are the
addresses iw3sp_mod calls into. Examples spotted in Functions.hpp:

```
R_BeginRemoteScreenUpdate   = 0x5DC550
R_EndRemoteScreenUpdate     = 0x5DC5A0
Com_PickSoundAliasFromList  = 0x581500
PM_AirMove                  = 0x5BF480
PM_UpdateSprint             = 0x5B72F0
G_RegisterWeapon            = 0x4B6140
BG_GetWeaponIndexForName    = 0x5BECE0
```

Plus dozens of `extern` data pointers: `aaGlobArray`, `cgs`,
`clients`, `keyNames`, `localizedKeyNames`, `playerKeys`, `ps`,
`pmove`, `g_entities`, `s_wmv`, `playersKb`, etc.

A `Functions.cpp` skim shows these are mostly initialised with literal
addresses too. The full file lists ~hundreds. Only a subset is touched
by gamepad code; we will pull them in *as needed* during Phase 2/3.

### Implication

For Phase 1 / Stage 1, the **19 active hook addresses are the
priority**. The wider helper map (~hundreds) is a tail of follow-ups
we discover as we port Gamepad.cpp -- each call into `Game::X` that
points at a literal address adds an item to the map.

## 3. Binaries to diff

| Binary | Size | SHA256 |
|---|---|---|
| `D:\Cod4Project\game-test\iw3sp.exe` | 3,035,136 | `7AEC4E2E3FA9ED188809D09FC08149C9024F6208E2F296149FFD0FC90757A2ED` |
| `D:\Cod4Project\game-test\iw3mp.exe` | 3,330,048 | `F50F8A520581754E73CB7CAE67B02ADCBAD5F2E71AAA412A013BB3EF57A391A5` |

iw3mp is **294,912 bytes larger** (~9.7%). Both are 32-bit PE
(`pei-i386`). Sibling builds of the same IW3 engine -- shared codebase
with SP/MP-specific differences. That overlap is exactly what makes
binary diffing tractable.

## 4. RE tool -- options and recommendation

### What is installed now

- **`objdump`** (from MinGW, on PATH via setup-env.bat) -- useful for
  dumps but not enough for full RE.
- **Ghidra, IDA, Cutter, Rizin, radare2 -- none installed.** No common
  install paths populated.

### Options

| Tool | License | Strength | Cost |
|---|---|---|---|
| **Ghidra** | Apache 2.0 (NSA) | Best free decompiler; built-in **Version Tracking** does function-matching between two binaries -- exactly our need | Needs JDK 17+; ~300 MB |
| Ghidra + **BinDiff** plugin | Both free | BinDiff is the canonical binary-diff tool; works as a Ghidra (or IDA Pro) plugin | Extra install on top of Ghidra |
| IDA Free 8.x | Proprietary (free non-commercial) | Hex-Rays decompiler (32-bit supported in modern Free); no scripting export limits in recent versions | Single installer; smaller than Ghidra; no BinDiff (BinDiff needs IDA Pro) |
| Cutter (Rizin) | GPL-3 | GUI for Rizin; lighter; decent diff via `rzdiff` | No formal function-matching like Version Tracking |
| radare2 (CLI) | LGPL-3 | Powerful but steep learning curve | Not suited to time-bounded work |

### Recommendation

**Ghidra alone, using its built-in Version Tracking.**
- Free; only Java SDK as a prerequisite.
- Version Tracking auto-matches functions between iw3sp.exe and
  iw3mp.exe by name (irrelevant, no symbols), signature, structure,
  references, and bytes. Yields a confidence-scored map.
- BinDiff is slightly better at matching but adds a second install
  and we only have 19 hot addresses -- Version Tracking is more than
  enough.

If Ghidra/JDK install proves heavy on the user's system, fallback to
IDA Free (single-installer experience; we then do function matching
by hand using the address lists -- slower but possible for 19 sites).

**Decision pending the user's approval before any tool install.**

## 5. Workflow

### 5.1 Storage of the address map

Use **`notes\stage3d-address-map.json`** (created this session). One
entry per hook site; fields:
```
iw3sp_addr, kind, iw3sp_mod_value, purpose,
iw3mp_addr (null until found),
verification (null until verified),
status: one of {todo, candidate, verified, absent, blocked}
gamepad_cpp_line  (for back-reference)
```
Optionally add `byte_signature` (the 8-16 bytes around the iw3sp site,
used as a pattern search in iw3mp.exe as a sanity check).

### 5.2 Per-address discovery procedure

For each iw3sp hook address:

1. **Identify the containing function in iw3sp.exe.** In Ghidra:
   navigate to address, find the enclosing function, note its name (if
   auto-named) and characteristic signature: prologue bytes, number of
   xrefs, calls it makes, locals/args inferred by decompiler.
2. **Find the matching function in iw3mp.exe.** Use Version Tracking
   (auto-match by signature/structure/bytes). For each hot site, accept
   only matches with reasonable confidence.
3. **Translate the hook address.** Hook sites are often mid-function
   (a specific call/load instruction). Inside the matched MP function,
   locate the equivalent instruction (a unique call to the same engine
   function, or a load of the same global pointer).
4. **Record `iw3mp_addr` + verification note.** E.g.:
   `"verified: call to CL_MouseMove at the same relative offset; byte
   pattern E8 ?? ?? ?? ?? matches at iw3mp 0x..."`.
5. **Spot-test** (Phase 2/3): for 3-5 sites, hook the iw3mp address
   from a throwaway debug build of `cod4x_021.dll` and confirm the
   expected behavior (e.g. `IN_Frame_Hk` log fires every frame). Catches
   misidentified addresses before we ship anything depending on them.

### 5.3 Verification methods (cheap-to-expensive)

1. **Byte-pattern search** -- if the iw3sp instruction has a short
   unique byte sequence, search iw3mp for it.
2. **String/xref cross-check** -- many engine functions reference
   unique strings (error messages, dvar names). Find the string in
   both binaries, follow xrefs to the using function, compare.
3. **Decompiler comparison** -- Ghidra's decompiled C side-by-side. A
   match should have the same shape (calls, branches, loop structure).
4. **Runtime probe** (most expensive but decisive) -- install the
   hook in cod4x_021.dll, run the game, look for the expected effect.

### 5.4 Triage rules

- `candidate`: a likely match found but not yet decisively verified.
- `verified`: at least two of the four methods above agree.
- `absent`: function genuinely doesn't exist in iw3mp.exe -- consider a
  redesign (re-implement vs port).
- `blocked`: cannot resolve; needs the user's judgment.

## 6. Updated time estimate (with the real numbers in hand)

| Step | Estimate |
|---|---|
| Install Ghidra (+ JDK if missing) | 30-60 min |
| Load iw3sp.exe + iw3mp.exe + autoanalyze | 15-30 min each (~1 h) |
| Version Tracking run + manual review | 1-3 h |
| Map the 19 hot addresses + verify | 3-5 h (~15-20 min per site) |
| Spot-test 3-5 sites at runtime | 1-2 h |
| **Phase 1 total** | **~1 day focused (6-10 h)** |
| Phase 2 (port `Gamepad.cpp` into `gamepad.c`/`gamepad.cpp` in CoD4x_Client_pub, hook with the discovered addresses) | 3-5 days |
| Phase 3 (preset .cfg + menus + integration test) | 2-3 days |
| Phase 4 (aim-assist polish, edge cases, MP-specific issues, anti-cheat checks) | 3-5 days |
| **Stage 3D total (revised)** | **~1.5-2 weeks** |

The previous estimate was the same envelope; the difference now is the
numbers are *evidenced* (we know it is 19 + tail, not "~30"), the tools
are scoped, and the workflow is concrete.

## 7. What is NOT done in Phase 1 (deliberately)

- No tool installed yet.
- No iw3mp address resolved yet.
- No engine analysis performed.
- No code written.

Phase 1 deliverable is the **map skeleton + workflow + tool decision**.
Phase 2 starts the actual discovery once the tool is installed (your
approval).

## 8. Decision points awaiting the user

1. **Approve Ghidra install** (Apache 2.0; needs JDK 17+ if absent).
   Pin a target install location (suggest `D:\Cod4Project\tools\ghidra\`
   to stay portable).
2. **Confirm scope** -- start Phase 2 with the 19 hot addresses only.
   The Functions.hpp tail (~hundreds) is pulled in incrementally during
   Phase 2 porting as each reference appears in `Gamepad.cpp`.
3. **Optional second mirror** for Ghidra (signed download from the
   official Ghidra release page) for SHA verification.
