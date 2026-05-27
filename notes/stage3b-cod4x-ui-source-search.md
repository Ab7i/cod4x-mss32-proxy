# Stage 3B -- search for CoD4x's customized UI source (read-only)

Goal: locate the `.menu` source that CoD4x uses to build its custom UI
patch zones (so we can extend it without losing CoD4x's UI mods).

**Result: that source is NOT publicly available.** But the search
clarified CoD4x's UI architecture, and that opens a clean path forward
that does not require it.

## 1. CoD4x_Client_pub (local + GitHub)

- `src/ui/` -- 2 headers only: `menudefinition.h` (CG_/UI_ display
  flag constants -- useful reference for any `.menu` we author) and
  `ui_menus.h` (75 bytes stub). **No `.menu` source.**
- `assets/cod4x/zone/` -- contains **pre-built fastfiles only**:
  - `cod4x_patch.ff`     26,284 bytes
  - `cod4x_patchv2.ff`  550,611 bytes
  - `cod4x_ambfix.ff`   496,286 bytes
  Committed as binary blobs; **no `.csv`, no `.menu` next to them.**
- `assets/cod4x/main/jcod4x_00.iwd` -- 67 entries: **29 `.iwi` images
  + 25 `.mp3` sounds**; zero menus, zero UI files. Asset iwd, not
  source.
- Project-wide `Glob` for `*.menu`: **0 matches.**

## 2. callofduty4x GitHub organization (16 repos)

Full list enumerated. None contain UI/menu source:

CoD4x-launcher (Rust loader), CoD4x_Server, CoD4x-mss, CoD4x_Client_pub
(checked above), cod4x_updaterdll_legacy, cod4x-docs, CoD4x_Client_new
(asm), cod4x_plugin_http, cod4x_landingpage, cod4x_docker, admintool,
mysql, public_sdk (lua + crypto libs, third-party SDK), mysql_storage,
cod4_trueskill_plugin, finalkillcam.

- `public_sdk` is unrelated (libraries for plugin authors).
- `cod4x-docs` mentions the GSC Mod Framework but **does NOT document**
  building `.menu` files, patch zones, or UI source availability.
- No repo named like `CoD4x_UI`, `CoD4x_Mod_Tools`, `cod4x_zone`,
  `ui_mp`, or anything pointing at UI source.

## 3. The architecture (corrected)

This search resolved a major question. CoD4x **does not modify the
stock `ui_mp.ff`**. It ships **additional patch zones** that load
alongside it and override individual menu assets:

```
zone\english\ui_mp.ff           (stock IW v1.7 retail, untouched)
zone\<somewhere>\cod4x_patch.ff      (CoD4x override layer)
zone\<somewhere>\cod4x_patchv2.ff    (further override / branding)
zone\<somewhere>\cod4x_ambfix.ff     (additional fix patch)
```

This also re-frames the ui_mp.ff size mystery from the previous report:
the live 420,146-byte `game-test\zone\english\ui_mp.ff` is almost
certainly the **stock IW v1.7 retail** UI (untouched), and our 926,610-
byte SDK rebuild is a **v1.4-era** ui_mp.ff (the mod-tools SDK is v1.4;
the live game is v1.7 -- the UI fastfile evolved between patches).
That size delta is a **version difference**, not a customization
difference. The previous "CoD4x stripped variant" hypothesis was wrong.

## 4. Implication for Stage 3B -- a much cleaner path

We do **not** need CoD4x's UI source. We do **not** need to rebuild
`ui_mp.ff`. We can ship a **new patch zone** that follows CoD4x's own
pattern -- same architecture, fully additive.

### Recommended approach -- additive patch zone

1. Author `gamepad.menu` (new file, ours) using the menudef syntax in
   `raw/ui_mp/*.menu` from the mod tools as reference.
2. Author a small `cod4x_gamepad.csv` listing just that menu.
3. Build `cod4x_gamepad.ff` with `linker_pc` (we just proved this
   works).
4. Ship it alongside the existing CoD4x patches in
   `fake-appdata\CallofDuty4MW\zone\`.
5. Open it from console with `\openmenu gamepad` (the `openmenu`
   command is commented out in `cl_main.c`; re-enabling is a tiny C
   change in `CoD4x_Client_pub`, same routine rebuild we have done many
   times).

### Why this is the clean path

- Touches **nothing** existing: no `ui_mp.ff` edit, no CoD4x patch
  override, no asset collision.
- Reuses CoD4x's own architectural pattern (patch zones).
- The smallest possible surface area for review and rollback.
- Stage 3C (a button in the Controls menu) is then a separate decision,
  *after* the gamepad menu itself works.

## 5. Risks and unknowns

- **Collision with CoD4x patches if we ever override an existing menu.**
  Our patch carries only a brand-new `gamepad.menu` -> no override ->
  no collision. The moment we want to add a button to `controls_multi`
  (Stage 3C), we hit the risk: if CoD4x's `cod4x_patchv2.ff` already
  overrides that menu, our override would collide. Unknown without
  inspecting the `.ff` (OpenAssetTools cannot dump IW3 menus, but it
  can list the assets in `cod4x_patchv2.ff` -- a worthwhile mini-spike
  before Stage 3C).
- **Fastfile version.** Our SDK rebuild reports `[ver. 5]`. The live
  CoD4x patches may be a different revision. The game appears tolerant
  of `ver. 5` (it loads retail v1.7 fastfiles), but a `ver` mismatch
  is a possible failure mode worth eyeballing after the first deploy.
- **Zone load order.** Where `cod4x_gamepad.ff` is placed and the order
  the game enumerates zones determines what wins on override. For an
  additive-only menu, order is moot. For Stage 3C overrides, order
  matters and needs verifying.
- **Re-enabling `openmenu`.** The command body itself is commented in
  `cl_main.c` (see `stage3b-menu-exploration.md` Q3); restoring it
  needs the function body and a `Cmd_AddCommand` line. Small but C
  work in `CoD4x_Client_pub` -- another rebuild + commit.

## 6. Bottom line / recommendation

- CoD4x's UI patch source is not public; do not block on it.
- Adopt CoD4x's own pattern: a new additive patch zone with just our
  new `gamepad.menu`.
- Open via `\openmenu gamepad` (re-enable the existing CoD4x command --
  one small C change).
- Defer the Controls-menu button (Stage 3C) until after the gamepad
  page itself is proven; that step needs an inspection of
  `cod4x_patchv2.ff` first.
