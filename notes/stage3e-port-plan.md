# Phase 3-E — port plan (BUTTON_* + bindings + key-name table + menu nav)

_Written 2026-05-28 after Phase 3-C complete. **Read-only analysis. No
code, no hooks installed.** Disassembly evidence from iw3mp.exe via
`DumpStage3E.java`._

---

## 0. TL;DR

- ~13 remaining `accepted` hooks. After excluding the 3 used by
  Phase 3-C and the 4 deferred (no_match/function_only/needs_manual/
  superseded), **12 hooks** remain for Phase 3-E across 7 distinct
  engine functions.
- **The big lesson from Phase 3-C holds and is worse here:** most of
  these are `__usercall` (register args) AND **mid-function** HOOK_JUMP/
  HOOK_CALL sites. iw3sp_mod implements **7 naked-asm trampolines** for
  them, each embedding **iw3sp-specific addresses** (jump-backs, data
  refs) that MUST be re-derived for iw3mp.
- **Infrastructure needed:** a NEW NASM file (`gamepad_stubs.asm`) wired
  into CMakeLists — the trampolines can't be expressed in C (they
  re-execute clobbered instructions, marshal register args, and jmp to
  mid-function addresses). regparm(1) alone is NOT enough this time.
- **Menu-nav subset exists** (≈ keyName table + GetLocalizedKeyName +
  Key_GetCommandAssignment) but the binding chain is interdependent;
  see §6.

## 1. The 12 hooks (from stage3d-address-map.json, `accepted`)

Excluded:
- Used by Phase 3-C: `0x463490` CL_MouseMove (forward), `0x463D70`
  CL_MouseMove_Stub site, `0x4FDC80` host.
- Deferred: `0x40A0B0` reload-hint (function_only), `0x4F92D7`
  Player_UseEntity (no_match), `0x5653CA` UI_RefreshStub (no_match),
  `0x5947A8` CL_MouseEvent_Stub (needs_manual).
- Superseded: `0x576193` IN_Frame (CoD4x owns IN_Frame).

| # | Purpose | iw3mp site | iw3sp_mod stub | kind |
|---|---|---|---|---|
| 1 | CL_KeyEvent (we CALL it) | `0x467EB0` (entry) | — (Hook::Call) | engine-call |
| 2 | CL_KeyEvent_Hk install | `0x4FDCBF` | CL_KeyEvent_Hk (plain C++) | HOOK_CALL |
| 3 | keyName table ptr #1 | `0x46777D` | — (Set) | Set<T*> |
| 4 | keyName table ptr #2 | `0x467785` | — (Set) | Set<T*> |
| 5 | keyName table ptr #3 | `0x467837` | — (Set) | Set<T*> |
| 6 | GetLocalizedKeyName #1 | `0x46782F` | GetLocalizedKeyName_Stub (naked) | HOOK_CALL |
| 7 | GetLocalizedKeyName #2 | `0x475E51` | GetLocalizedKeyName_Stub02 (naked) | HOOK_CALL |
| 8 | Key_GetCommandAssignment | `0x4678E0` (entry) | Key_GetCommandAssignmentInternal_Stub (naked) | HOOK_JUMP |
| 9 | Key_WriteBindings | `0x4FFB0F` | Key_WriteBindings_Stub (naked) | HOOK_JUMP |
| 10 | Key_SetBinding #1 | `0x5529B8` | Key_SetBinding_stub01 (naked) | HOOK_JUMP |
| 11 | Key_SetBinding #2 | `0x5529CB` | Key_SetBinding_stub02 (naked) | HOOK_JUMP |
| 12 | Key_SetBinding #3 | `0x5529E3` | Key_SetBinding_stub03 (naked) | HOOK_JUMP |

## 2. Category + calling convention per hook (disassembly-verified)

Categories: **A** = entry hook, C-expressible (cdecl/regparm). **B** =
mid-function OR register-marshalling → naked-asm trampoline. **C** =
Set<T*> pointer/operand overwrite (Patch_SetPtr). **D** = NOP (none here).

| # | Hook | Category | Convention (from disasm) | Trampoline? | Complexity |
|---|---|---|---|---|---|
| 1 | CL_KeyEvent `0x467EB0` | engine-call | **__cdecl**, 4 stack args. Entry: `MOV EAX,[ESP+0xC]` (reads 3rd arg from stack, NOT EAX-input). | no | **LOW** |
| 2 | CL_KeyEvent_Hk `0x4FDCBF` | A | site = `CALL 0x467EB0` + `ADD ESP,0x10` (cdecl, 4 args). Replace with `CALL our_hk`; hk is plain `__cdecl(int,int,int,unsigned)` forwarding to `0x467EB0`. | no | **LOW** |
| 3-5 | keyName ptrs `0x46777D/85`, `0x467837` | C | mid-instruction (operand of `MOV reg,imm32`); `getInstructionAt` returns null = operand bytes. Overwrite imm32 with our `combinedKeyNames` via `Patch_SetPtr`. | no | **LOW** (patch) + **MED** (build table) |
| 6 | GetLocalizedKeyName#1 `0x46782F` | B | mid-fn (+0x6F in `0x4677C0`). site = `MOV EAX,0x727248` (loads stock localized-keyname table). Followed by `JNZ` (flag-dependent!). Naked stub redirects EAX to our map + must preserve flags for the JNZ. | **YES naked** | **HIGH** |
| 7 | GetLocalizedKeyName#2 `0x475E51` | B | mid-fn (+0x91 in `0x475DC0`). site = `MOV EAX,0x727248` then `JZ`. Same flag-sensitivity. | **YES naked** | **HIGH** |
| 8 | Key_GetCommandAssignment `0x4678E0` | B | FUNCTION-ENTRY. `0x4678E1: IMUL EAX,EAX,0xd28` -> **__usercall, arg1=EAX (localClientNum)** + stack args. Naked stub marshals EAX+stack to the C++ impl (full re-impl). | **YES naked** | **MED** |
| 9 | Key_WriteBindings `0x4FFB0F` | B | mid-fn (+0x5F in `0x4FFAB0`). site = `LEA ECX,[ESP+0x24]` (4B) + `PUSH ECX` (1B) = 5B overwritten. Naked stub does hk work then re-executes LEA+PUSH? (iw3sp jumps to site+5 WITHOUT re-exec -> must re-verify for iw3mp). | **YES naked** | **MED-HIGH** |
| 10-12 | Key_SetBinding `0x5529B8/CB/E3` | B | mid-fn (+0x98/+0xAB/+0xC3 in `0x552920`). All three sites = `CALL 0x4678b0` (clean 5B). __usercall: stub01/02 push ECX+EAX, stub03 pushes EDX+ECX+EAX -> Key_SetBinding_Hk. jump-back = site+5. | **YES naked ×3** | **MED** |

**Verdict on conventions:** CL_KeyEvent is __cdecl (good — we call it
plainly). Key_GetCommandAssignment and Key_SetBinding are __usercall
(EAX / ECX+EAX / EDX+ECX+EAX). GetLocalizedKeyName sites are
flag-sensitive mid-function patches. **8 of 12 need naked asm.**

## 3. The naked-asm trampolines (iw3sp_mod source -> iw3mp port work)

iw3sp_mod has these `__declspec(naked)` stubs we must re-create as NASM
(MinGW cdecl symbols, like `client_callbacks.asm`). Each embeds iw3sp
addresses to TRANSLATE:

| Stub | iw3sp embedded addrs | iw3mp translation needed |
|---|---|---|
| Key_GetCommandAssignmentInternal_Stub | (none; pure marshalling) | jump-back not needed (entry re-impl); just `ret` |
| GetLocalizedKeyName_Stub | re-exec `test edi,edi` | **re-derive the iw3mp flag-setting instr before `0x46782F`** |
| GetLocalizedKeyName_Stub02 | re-exec `cmp ds:0x6DFE30,0` | translate `0x6DFE30` -> iw3mp; re-derive instr |
| Key_WriteBindings_Stub | `jmp 0x535384` | jump-back = `0x4FFB0F + 5 = 0x4FFB14`; verify LEA+PUSH handling |
| Key_SetBinding_stub01 | `jmp 0x56F72D` | jump-back = `0x5529B8 + 5 = 0x5529BD` |
| Key_SetBinding_stub02 | `jmp 0x56F740` | jump-back = `0x5529CB + 5 = 0x5529D0` |
| Key_SetBinding_stub03 | `jmp 0x56F758` | jump-back = `0x5529E3 + 5 = 0x5529E8` |

Plus the C++ "Hk" helpers that the stubs call (NOT naked, portable C):
`Key_GetCommandAssignmentInternal`, `GetLocalizedKeyNameMap`,
`Key_SetBinding_Hk`, `Key_WriteBindings_Hk`, `Gamepad_WriteBindings_Hk`,
`CL_KeyEvent_Hk`, plus support: `GetGamePadCommand`,
`Key_IsValidGamePadChar`, `CreateKeyNameMap`, the String->Enum axis
parsers, `Axis_Bind_f`/`Axis_Unbindall_f` commands, and ~5 dvars
(gpad_style, gpad_sticksConfig, gpad_buttonConfig, etc.).

## 4. Additional engine addresses to resolve (data refs, not yet mapped)

These appear inside the C++ helpers / naked stubs and need iw3mp VAs
(via Ghidra) before implementation:
- `Game::playerKeys` (engine per-client key state array) — used by
  Key_GetCommandAssignmentInternal. **NEW engine-ABI dependency** (also
  needed by Path B later). Struct layout (keys[].binding) must match.
- `Game::keyNames`, `Game::KEY_NAME_COUNT`, `Game::localizedKeyNames`,
  `Game::LOCALIZED_KEY_NAME_COUNT` — for CreateKeyNameMap (build the
  combined table). `0x727248` (stock localized table) already seen in
  the iw3mp disasm.
- `Game::K_LAST_KEY`, the K_BUTTON_*/K_DPAD_*/K_APAD_* keynums (already
  in gamepad_internal.h as GAME_K_*).
- `FS_Printf`, `Key_WriteBindings`, `Key_SetBinding`, `Cbuf_*` engine
  fns (some already in the map / used by 3-C).
- iw3sp data `0x6DFE30` (GetLocalizedKeyName_Stub02) -> iw3mp twin.

## 5. Infrastructure needed

1. **`src/gamepad_stubs.asm`** (NEW, NASM win32, `--prefix _`) — the 7
   naked trampolines. Wire into CMakeLists `ASM_NASM` sources (next to
   `client_callbacks.asm`). This is the file the parallel session
   created prematurely; we now build it correctly with iw3mp-derived
   addresses + per-site flag/jump-back handling.
2. **`Patch_Nop`** — NOT needed for Phase 3-E (no Hook::Nop in this
   set; the Nop was for the deferred 0x5947A8).
3. `Patch_SetPtr` (exists, 3-A) — for the 3 keyName table operands.
4. `Patch_SetCall`/`Patch_SetJump` (exist, 3-C, self-unprotecting) —
   for HOOK_CALL/JUMP installs.
5. **A per-hook disassembly pre-flight** (like DumpCallSite.java) is
   MANDATORY for each naked stub to capture the exact overwritten bytes
   + the flag-setting predecessor instruction. This is the bulk of the
   risk and the work.

## 6. Menu-navigation subset (user's actual priority)

The "controller doesn't work in the main menu / shows as Auxiliary X"
problem maps to this dependency chain:

- **Glyph names in menus** (BUTTON_A instead of "Auxiliary 1"):
  hooks 3-7 (keyName table ×3 + GetLocalizedKeyName ×2) +
  CreateKeyNameMap.
- **Gamepad keys resolve to menu commands when pad in use:**
  hook 8 (Key_GetCommandAssignment) + GetGamePadCommand +
  Key_IsValidGamePadChar + `Game::playerKeys`.
- **Binding gamepad keys persists to config:** hooks 9-12
  (Key_WriteBindings + Key_SetBinding ×3) + bindaxis command.

**Can we do a minimal menu-nav subset?** Partially:
- A TRUE minimal "menus respond to the controller" may already be
  closer than it looks — Phase 3-B already delivers gamepad buttons as
  `K_JOY*` via Com_QueueEvent, so menus that have `K_JOY*` bindings
  would navigate. The iw3sp_mod chain instead uses the ENGINE keynums
  (K_BUTTON_A=0x1 ...) and makes the stock menus recognize them.
- **Open question for the user:** do we (a) port the full iw3sp_mod
  engine-keynum binding chain (hooks 3-12, the "correct"/console-faithful
  path, heavy), or (b) attempt a lighter CoD4x-native menu-nav using the
  existing K_JOY* events + menu bindings in the .menu files (Phase 4
  territory, less engine surgery)? This decides whether Phase 3-E is
  "all 12 hooks" or a thin slice.

**Recommendation:** spike hook 2 (CL_KeyEvent_Hk, LOW) + hooks 3-5
(keyName table, LOW/MED) + hook 8 (Key_GetCommandAssignment, MED) as a
"binding core" first — these unlock proper BUTTON_* names and command
resolution. Defer GetLocalizedKeyName glyph rendering (HIGH) and
Key_WriteBindings/SetBinding persistence to a second 3-E pass once the
core proves out in-menu.

## 7. Proposed ordering (easiest first, de-risked)

| Step | Hooks | Why first | Asm? |
|---|---|---|---|
| 3-E.1 | #1 CL_KeyEvent decl + #2 CL_KeyEvent_Hk | LOW, cdecl, no asm; marks pad in/out of use (foundation for the rest) | no |
| 3-E.2 | #3-5 keyName table + CreateKeyNameMap | LOW patch + table build; gives BUTTON_* names. Needs keyNames/localizedKeyNames VAs | no |
| 3-E.3 | #8 Key_GetCommandAssignment | First naked stub (entry, __usercall EAX). Needs `Game::playerKeys` VA + struct | **yes** |
| 3-E.4 | #10-12 Key_SetBinding ×3 | 3 near-identical naked stubs (ECX/EAX/EDX) | **yes** |
| 3-E.5 | #9 Key_WriteBindings | mid-fn naked, config persistence | **yes** |
| 3-E.6 | #6-7 GetLocalizedKeyName ×2 | HIGH (flag-sensitive mid-fn); glyph rendering, least critical | **yes** |

Each step: disassemble pre-flight -> port -> build -> in-game test ->
commit. Same discipline as Phase 3-C.

## 8. Risks (Phase 3-C lessons applied)

| Risk | Mitigation |
|---|---|
| __usercall register args (EAX/ECX/EDX) — repeat of the 3-C crash | Disassemble EVERY site's entry + call setup before writing; naked stubs marshal the exact registers iw3sp_mod's stubs do |
| Naked stub embeds iw3sp addresses (jump-backs, `0x6DFE30`, flag re-exec) | Each must be re-derived from iw3mp disasm; pre-flight per stub |
| `Game::playerKeys` struct layout differs SP->MP | Byte-verify offsets (binding ptr) before Key_GetCommandAssignment; this is the same engine-ABI care Stage 3-D needs |
| GetLocalizedKeyName flag-sensitivity (JNZ/JZ after the patched MOV) | The stub must restore EFLAGS or re-execute the iw3mp flag-setting instruction; isolate-test with gpad_debug |
| Mid-function 5-byte patch splits an instruction | Verify each site's instruction length == 5 (CALL/MOV imm32 = 5 ✓; LEA+PUSH = 4+1 = 5 ✓ for #9) |
| config_mp.cfg corruption from Key_WriteBindings | Test against a backup profile; the write path appends `bindaxis`/bind lines |

## 9. Time estimate

| Step | Sessions |
|---|---|
| 3-E.1 (CL_KeyEvent + Hk) | 0.5 |
| 3-E.2 (keyName table + CreateKeyNameMap) | 1 |
| 3-E.3 (Key_GetCommandAssignment + first asm) | 1 |
| 3-E.4 (Key_SetBinding ×3) | 1 |
| 3-E.5 (Key_WriteBindings) | 0.5-1 |
| 3-E.6 (GetLocalizedKeyName ×2) | 1 |
| **Total** | **~5 sessions** |

(vs Phase 3-C's effective ~4 incl. the crash detour. 3-E is heavier:
8 naked stubs + a new engine-ABI struct (`playerKeys`).)

## 10. Decisions for the user (before any code)

1. **Full chain (hooks 3-12) vs minimal menu-nav slice?** (§6) — i.e.
   port iw3sp_mod's engine-keynum binding subsystem, or attempt a
   lighter CoD4x-native menu navigation via existing K_JOY* events +
   .menu bindings (which leans into Phase 4)?
2. **Ordering:** accept the easiest-first order in §7, or front-load the
   specific menu-nav hooks?
3. **`Game::playerKeys` engine-ABI:** Phase 3-E is the first hook set
   that reads engine `playerKeys` directly (Path B territory). OK to
   take that on now, or keep 3-E to the Set/cdecl hooks and push the
   __usercall/playerKeys ones toward 3-D?
4. **New asm file:** confirm we add `src/gamepad_stubs.asm` (NASM) — the
   trampolines genuinely cannot be C this time.

---
_Status: Phase 3-C complete (commit 22eb6b4, tag phase3c-complete).
Phase 3-E NOT started. This is analysis only -- awaiting user decisions
in §10 before any implementation._
