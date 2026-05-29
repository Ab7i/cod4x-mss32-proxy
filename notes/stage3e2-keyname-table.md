# Phase 3-E.2 -- keyName table extension -- ANALYSIS

_Written 2026-05-28. Read-only. No code, no build. Same disasm-first
discipline as Phase 3-C / 3-E.1._

---

## 0. TL;DR -- the iw3sp_mod recipe does NOT port 1:1 (CoD4x conflict)

iw3sp_mod extends the engine keyName table by building a combined table
(stock 95 entries + 16 gamepad names + null) and repointing the engine's
lookup functions via 3 `Set<keyname_t*>` operand patches + 2
GetLocalizedKeyName stubs. **In CoD4x that only covers HALF the
consumers**: CoD4x **reimplements** the forward lookup
(`Key_KeynumToString`) in `cl_keys.c`, reading the stock table addresses
(0x726F48 / 0x727248) via **hardcoded macros**. So patching the engine's
functions doesn't reach CoD4x's own lookup. There are TWO independent
consumers of the keyName data with TWO different code owners.

This is the same class of finding as IN_Frame (3-C) and CL_KeyEvent
(3-E.1): CoD4x reimplements a chunk the engine owned, so the iw3sp_mod
hook lands on dead/secondary code.

## 1. keyname_t + the data (verified)

`keyname_t` (matches our `gp_keyname_t` already in gamepad_internal.h):
```c
typedef struct { const char *name; int keynum; } keyname_t;
```

iw3sp_mod tables (Functions.cpp):
- `keyNames`          @ iw3sp 0x6DFB30   (stock, KEY_NAME_COUNT = 95)
- `localizedKeyNames` @ iw3sp 0x6DFE30   (LOCALIZED_KEY_NAME_COUNT = 95)

iw3mp tables (from CoD4x cl_keys.c hardcoded macros + disasm):
- `keynames`            @ **iw3mp 0x726F48**  (stock english)
- `keynames_translated` @ **iw3mp 0x727248**  (localized)
- `specialKeynameTable` @ iw3mp 0x727548

Both tables are **null-terminated** arrays (CoD4x's Key_KeynumToString
iterates `for (; kn->name; kn++)`), so we can copy until null instead of
trusting a hardcoded count -- safer across SP/MP.

Extended entries iw3sp_mod adds (Gamepad.cpp:92, 16 entries):
```
BUTTON_A=0x1, BUTTON_B=0x2, BUTTON_X=0x3, BUTTON_Y=0x4,
BUTTON_LSHLDR=0x5, BUTTON_RSHLDR=0x6, BUTTON_START=0xE, BUTTON_BACK=0xF,
BUTTON_LSTICK=0x10, BUTTON_RSTICK=0x11, BUTTON_LTRIG=0x12, BUTTON_RTRIG=0x13,
DPAD_UP=0x14, DPAD_DOWN=0x15, DPAD_LEFT=0x16, DPAD_RIGHT=0x17
```
(All already defined as GAME_K_* in gamepad_internal.h.) Plus separate
localized tables with Xbox/PS3 glyph material tags (extendedLocalizedKey
NamesXenon/Ps3) -- those are the Phase 3-E.6 glyph work, NOT this step.

## 2. The 3 Set<keyname_t*> sites (iw3mp, from address map)

| iw3sp Set site | iw3mp site | inside fn | what it patches |
|---|---|---|---|
| 0x44367D | **0x46777D** | 0x4676F0 (+0x8D) | operand of `MOV reg, <keyNames>` |
| 0x443685 | **0x467785** | 0x4676F0 (+0x95) | operand of `MOV reg, <keyNames>` |
| 0x443737 | **0x467837** | 0x4677C0 (+0x77) | operand of `MOV reg, <keyNames>` |

DumpStage3E.java confirmed these addresses sit MID-INSTRUCTION
(getInstructionAt = null) -- they are the imm32 operand bytes of
`MOV reg, 0x726F48`-style loads. Patchable with `Patch_SetPtr(site,
combinedTable)` (operand overwrite, exactly Category C). Function
0x4676F0 / 0x4677C0 are the engine's `Key_StringToKeynum` /
`Key_KeynumToString` (the ENGINE copies, used by the engine's bind
parser + reverse lookup).

## 3. THE CONFLICT -- CoD4x reimplements the forward lookup

`cl_keys.c`:
```c
#define keynames            ((keyname_t*)(0x726F48))   // hardcoded
#define keynames_translated ((keyname_t*)(0x727248))
const char *Key_KeynumToString(int keynum, qboolean bTranslate) {
    ...
    kn = keynames;                 // or keynames_translated
    for (; kn->name; kn++)
        if (keynum == kn->keynum) return kn->name;
    ...                            // else hex string
}
int Key_GetCommandAssignment(int localClientNum, char *search, int *twokeys) { ... }  // CoD4x's own
```

So CoD4x has its OWN `Key_KeynumToString` (keynum -> name) reading the
stock table at a **hardcoded immediate** (not a patchable pointer
variable). iw3sp_mod's 3 `Set` patches only repoint the ENGINE's
0x4676F0/0x4677C0 functions; they do NOT touch CoD4x's cl_keys.c
function. => Two consumers, two owners:

| consumer | owner | reads table via | covered by iw3sp_mod Set patches? |
|---|---|---|---|
| keynum -> name (console bind list, Key_KeynumToString) | **CoD4x cl_keys.c** | hardcoded macro 0x726F48 | **NO** |
| name -> keynum (`bind BUTTON_A`, engine Key_StringToKeynum) | engine 0x4676F0 | MOV imm32 (the 3 Set sites) | yes |
| menu glyph (GetLocalizedKeyName) | engine 0x46782F/0x475E51 | MOV 0x727248 | (3-E.6, deferred) |

`Key_StringToKeynum` is NOT reimplemented in cl_keys.c (no match) -> the
`bind <name>` reverse lookup is the engine's, so the 3 Set patches DO
help binding. But the forward NAME display goes through CoD4x's copy and
needs a separate fix.

## 4. Revised implementation for CoD4x (NOT iw3sp_mod's 3-patch recipe)

Build the combined tables in our memory, then repoint BOTH owners:

1. **Build** `gp_combined_keynames[]` = copy stock `keynames` (iterate
   until null) + 16 `extendedKeyNames` + null terminator. (Same for the
   localized table, but that's used by 3-E.6 -- can defer.)
2. **Engine side (reverse lookup / bind):** `Patch_SetPtr` the 3 sites
   (0x46777D / 0x467785 / 0x467837) -> `gp_combined_keynames`. (Category
   C, Patch_SetPtr exists since 3-A. Pre-flight: confirm each site holds
   imm32 == 0x726F48 before patching.)
3. **CoD4x side (forward name display):** edit `cl_keys.c` so its
   `keynames` macro / Key_KeynumToString reads `gp_combined_keynames`
   instead of the hardcoded 0x726F48. Options:
   - (a) change the macro to our table pointer, or
   - (b) make Key_KeynumToString fall through to a gamepad-name lookup
     for keynums 0x1..0x17 when the stock table misses.
   Recommend (b): a minimal, additive change -- after the stock loop
   misses, check our 16 extended names. Avoids relocating the whole
   table and keeps cl_keys.c's stock behavior intact.

### Where our data lives
- New TU `src/gamepad_keys.c` (build + own the combined table; expose a
  `gp_keynum_to_name(keynum)` helper for the cl_keys.c fallback) +
  prototype in gamepad.h. Avoids bloating gamepad.c.

## 5. Conflicts / risks

| Risk | Note |
|---|---|
| **Dual consumer (the headline)** | engine reverse-lookup (Set sites) + CoD4x forward-lookup (cl_keys.c). Must patch both or naming/binding will be half-broken (e.g. `bind BUTTON_A` works but the controls list shows "0x01"). |
| Set site no longer == 0x726F48 in iw3mp | Pre-flight DumpKeynameSites.java: read imm32 at each of the 3 sites; abort if != 0x726F48 (the SP offsets +0x8D/+0x95/+0x77 may not land exactly on the iw3mp operand). |
| Table lifetime | `gp_combined_keynames` must be static/global and outlive all lookups (engine holds the pointer indefinitely). |
| Localized/glyph table | Deferred to 3-E.6 (HIGH, naked GetLocalizedKeyName stubs + flag-sensitivity). 3-E.2 does the ENGLISH table only. |
| Does this even help nav? | NO -- naming/binding foundation only. Navigation is a separate problem (still unresolved from the 3-E.0 menu investigation). 3-E.2 unblocks 3-E.3/3-E.4 (binding) + 3-E.6 (glyphs). |

## 6. Implementation plan + steps

1. Pre-flight: `DumpKeynameSites.java` -- dump imm32 at 0x46777D/85,
   0x467837 (confirm == 0x726F48) + dump the stock table at 0x726F48 to
   read KEY_NAME_COUNT in iw3mp (count to null).
2. `src/gamepad_keys.c`: build `gp_combined_keynames[]` at init
   (copy-stock-until-null + append 16 + null), + `gp_keynum_to_name`.
3. `Patch_SetPtr` the 3 engine sites (self-unprotecting, exists).
4. `cl_keys.c`: additive fallback in Key_KeynumToString for the 16
   gamepad keynums (or repoint the macro).
5. Call the table builder once from IN_StartupGamepads (or a new init).
6. Diagnostic: log "BUTTON_A -> 0x1" round-trip both directions.
7. Build, backup, deploy, test (`bind BUTTON_A "+attack"` resolves;
   controls list shows BUTTON_A not 0x01).

## 7. Estimate
**2 sessions** (was 1-2): the CoD4x dual-consumer split + the cl_keys.c
edit + pre-flight push it past a single session. Still no naked asm, no
engine-ABI struct.

## 8. Decisions for the user
1. **Do we need the FORWARD name display now (cl_keys.c edit)**, or is
   the engine reverse-lookup (3 Set sites) enough for the immediate goal
   (so `bind BUTTON_A` works even if the list still shows 0x01)?
   - Minimal: just the 3 Set sites (enables `bind BUTTON_A`).
   - Full: + cl_keys.c fallback (proper names everywhere).
2. New TU `gamepad_keys.c` for the table, or fold into gamepad.c?
3. Confirm editing `cl_keys.c` (core CoD4x, additive fallback) is OK --
   same sanctioned "CoD4x reimplemented it, edit directly" path as
   common.c in 3-E.1.
4. **Reminder:** the menu-nav root cause from 3-E.0 is still open (the
   verbose Key_Event diagnostic build `480B28AE` is currently deployed,
   spammy). Do we want to resolve nav first, or keep building the
   binding foundation (3-E.2) and return to nav later?

---
_Status: Phase 3-E.1 verified. Phase 3-E.2 analysis only -- no code.
Key finding: CoD4x reimplements Key_KeynumToString (cl_keys.c, hardcoded
0x726F48), so iw3sp_mod's 3-Set recipe covers only the engine reverse
lookup; the forward name display needs a separate cl_keys.c fix. Awaiting
decisions in section 8._
