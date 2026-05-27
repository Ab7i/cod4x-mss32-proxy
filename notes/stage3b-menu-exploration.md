# Stage 3B -- menu system exploration (read-only, no code)

Goal of Stage 3B: a separate "Gamepad" page in the Controls menu --
dropdowns per button, sensitivity/deadzone sliders, toggles, a "Reset to
Defaults" button -- fully separate from the keyboard binding list.

This document is the "understand the problem" pass. **Key result: the
problem is materially bigger than assumed, and partly blocked on tooling
+ a verification test.** Details below.

## 1. Where the menus actually live

**Correction of an earlier assumption.** `input-pipeline-discovery.md`
said the `.menu` files sit loose inside `iw_*.iwd` as `ui_mp/*.menu`.
**That is wrong.** Verified:

- Scanned every archive -- `main\*.iwd` (incl. localized), `Mods\`,
  `fake-appdata\main\` -- for `.menu` entries: **none.** `iw_*.iwd`
  contain only `.iwi` (textures) and `.cfg`.
- The menus are **compiled into fastfiles**: `zone\english\ui_mp.ff`
  (420 KB) and `ui.ff` (53 KB). A `.ff` is a zlib-compressed CoD4
  asset archive.
- In CoD4x source, menus are xassets: `ASSET_TYPE_MENULIST` /
  `ASSET_TYPE_MENU`, loaded from `.ff` by `Load_MenuList()` /
  `MenuLoadMenuDef_t()` (`xasset_loader.c`).

So there is no loose `.menu` file to copy as a model, and no loose file
to drop into a mod iwd as-is.

## 2. The runtime `.menu` infrastructure in CoD4x -- mostly disabled

- The menu **parser is live**: `UI_ParseMenuOfMemory()` ->
  `PC_LoadSourceHandleA()` -> `Menu_New()` / the `"menudef"` token
  handler (`ui_main.c`). Compiled in the public build.
- `files.c` `FS_FilesAreLoadedGlobally()` lists `.menu` among the
  extensions the engine treats as loose global files -- so the engine
  *has a concept of* loose `.menu` files.
- **But every command that would use it is commented out:**
  - `Cmd_AddCommand("loadmenu", ...)`, `("openmenu", ...)`,
    `("dumpxassets", ...)` -- `cl_main.c:1117-1121`, inside `/* */`.
  - `UI_LoadMenu_f()` itself -- `cl_main.c:179-188`, commented.
  - `UI_DumpMenus_f()` (dumps loaded menus to
    `dumps/<gamedir>/menus/*.menu`) -- `ui_main.c:1029-1053`, commented.
- Net: the engine *can* parse a `.menu`, but the public build exposes
  **no command** to load a loose menu at runtime or to dump the
  compiled ones.

## 3. Extracting the current Controls menu (to use as a model)

Two ways, both with a cost:

- **External `.ff` extractor.** Extract `ui_mp.ff` with a community
  CoD4 fastfile tool, locate the controls menu asset, read its menudef
  text. No such tool is bundled (`tools\` has only cmake/mingw/nasm/
  rust; `lld-link.exe` is the LLVM linker, unrelated).
- **Re-enable `UI_DumpMenus_f`.** Uncommenting it (a few lines of C) +
  a rebuild would dump every loaded menu -- including Controls -- as
  `.menu` text. This is a C change, so it is out of scope for this
  (exploration-only) session, but it is the cheapest path to the real
  syntax.

Until one of these is done, the exact menudef syntax, the slider/list
item types and how items bind to cvars are **not directly observable**.

## 4. Risks and unknowns (honest)

1. **Slider range 1-10 / dropdowns** -- cannot be answered yet. The
   menudef item types are locked inside `ui_mp.ff`. CoD4's menu system
   does have slider and multi/list item types and dvar bindings
   (general engine knowledge), but the exact item kinds and whether a
   clean 1-10 integer slider is expressible needs the extracted syntax.
2. **Loose `.menu` loading is unverified.** Whether the retail engine +
   CoD4x will load a loose `ui_mp/*.menu` from a mod iwd *without*
   re-enabling the commented code is UNKNOWN. Must be tested, not
   assumed. The user's premise ("ship a loose menu in a mod iwd, never
   touch the original") is not confirmed and is in tension with the
   menus being `.ff`-compiled + the load commands disabled.
3. **"Add a Gamepad button to the Controls menu"** means editing the
   *existing* Controls menudef -- which lives in `ui_mp.ff`. A separate
   loose file cannot add a button to a menu it does not own. This
   forces either an `ui_mp.ff` rebuild or a code-side menu hook.
4. **Removing "Auxiliary 1-16" from Controls > Combat** likewise edits
   the existing keyboard-bind menu -- same `.ff` problem.

## 5. Proposed plan -- verification-first

Stage 3B cannot be costed accurately until two facts are known: the real
menudef syntax, and whether loose `.menu` loading works. So:

**Step 0 -- verification spike (do this first).**
- Get the Controls menudef text: either an external `.ff` extractor, or
  re-enable `UI_DumpMenus_f` (1 small C change + rebuild) and dump.
- Test loose-menu loading: re-enable `loadmenu`/`openmenu` (small C
  change) OR test whether a loose `ui_mp/<name>.menu` in a mod iwd is
  picked up. Determine the real loading mechanism.
- Output: real syntax in hand + a yes/no on loose loading.

**Then one of:**
- **Path A -- `.ff` rebuild.** Use CoD4 mod tools (`linker_pc`) to
  rebuild `ui_mp.ff` with a new gamepad menudef + an edited Controls
  menu. Standard for CoD4 menu mods; needs the mod tools; ships a
  replacement `ui_mp.ff`.
- **Path B -- code-side menu injection.** Re-enable CoD4x's menu-load
  path and register/open a loose `controller.menu` from C, plus a hook
  to add the entry into the Controls menu. More CoD4x C work, partly
  uncharted.

Recommendation: **do Step 0 next session.** It is small, decisive, and
turns every "unknown" above into a fact. Picking A vs B before that is
guesswork.

## 6. Rough time estimates

| Work | Estimate |
|---|---|
| Step 0a: extract/dump the Controls menu text | 1-3 h (tool hunt or 1 C uncomment + build) |
| Step 0b: verify loose `.menu` loading mechanism | 1-3 h (experiment) |
| Understand menudef syntax from the real file | 1-2 h |
| Path A: author menudefs + rebuild `ui_mp.ff` | 1-2 days (incl. mod-tools setup) |
| Path B: re-enable + C-side menu hook + page | 1-2 days, uncertain |

## 7. Bottom line for review

- The clean "loose `.menu` in a mod iwd, never touch originals" plan is
  **not confirmed possible** and likely is not, as stated.
- Stage 3B is a multi-day effort once the path is chosen, gated on a
  short verification spike (Step 0).
- No `.menu` files, no menudef syntax and no slider/dropdown answers are
  available without first extracting `ui_mp.ff` or re-enabling the
  dump code -- both beyond an exploration-only session.

## 8. Step 0 investigation -- Option B is a dead end (verified)

Read `xasset_loader.c` (`DumpXAsset`, `DumpXAssets`, `MenuDumpMenuDef_t`)
and `cl_main.c` (`UI_DumpXAssets_f`).

### What CoD4x's menu dump actually produces

- `DumpXAsset()` (xasset_loader.c:360) for `ASSET_TYPE_MENU` uses
  `dumpproc = MenuDumpMenuDef_t` -- the `XAssetHeader` version, line 642.
- `MenuDumpMenuDef_t(XAssetHeader)` serializes the menu into a binary
  stream (`MSG_Init` + `XAssetStore*` / `XAssetStorePointer` calls).
- `DumpXAsset` writes a file: magic `"IW3XASSET"` + a binary
  `XAssetFileHeader_t` + a dependency string table + the binary asset
  body. Path: `dumps/<gamedir>/menu/<name>.menu` (under fs_homepath:
  `game-test\dumps\main\menu\*.menu`).
- **That `.menu` file is BINARY** (IW3XASSET format). The extension is
  misleading -- it is not menudef source text.

### There is no menudef -> text serializer in CoD4x

- The text-shaped `MenuDumpMenuDef_t(menuDef_t*, char* asname, char*
  outdata, int)` (ui_main.c:1027) and `MenuDumpMenuDefBin_t`
  (ui_main.c:1028) are **declared but never defined** anywhere.
- `UI_DumpXAssets_f` (cl_main.c:160) is an empty stub -- its whole body
  is commented out; `dumpxassets` is not registered; `DumpXAssets()`
  takes no arguments (type filter commented), so the stale commented
  `DumpXAssets(ASSET_TYPE_MENU)` call would not even compile.

### Verdict

Re-enabling CoD4x's dump (Option B) yields **binary IW3XASSET files**,
not readable menudef syntax. It cannot answer "what does the Controls
menudef look like / are 1-10 sliders and dropdowns supported." **Option B
as scoped is a dead end for the goal.**

### The actual path: CoD4 Mod Tools

CoD4 menudef *source* is not recovered from a `.ff`. The official **CoD4
Mod Tools** ship it directly: `raw/ui_mp/*.menu` and `raw/ui/*.menu` are
plain-text, editable menudef source (including `controls_*.menu`). The
Mod Tools also include `linker_pc`, which compiles `raw/` into a
`ui_mp.ff`. This is the standard, intended CoD4 menu-modding pipeline --
neither "Option A" (a .ff extractor) nor "Option B" (CoD4x's dump). It
is the real SDK.

Recommendation: obtain the CoD4 Mod Tools; use `raw/ui_mp/*.menu` as the
model and `linker_pc` to rebuild `ui_mp.ff` for Stage 3B.
