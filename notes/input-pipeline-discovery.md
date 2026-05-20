# Input pipeline discovery — exploration only (no code written)

Goal: locate, inside `CoD4x_Client_pub` source (which patches `iw3mp.exe`
and ships as `cod4x_021.dll`), the three subsystems needed for native
controller support. All findings below are from reading the C source.

Scope note: this is a read/analysis pass. No code was written.

---

## Q1 — The cvar system

### Where cvars are defined / registered

- File: `src/cvar.c`. Public registration wrappers:
  - `Cvar_RegisterBool (name, bool, flags, desc)`            -> CVAR_BOOL
  - `Cvar_RegisterInt  (name, int, min, max, flags, desc)`   -> CVAR_INT
  - `Cvar_RegisterFloat(name, val, min, max, flags, desc)`   -> CVAR_FLOAT
  - `Cvar_RegisterString(name, str, flags, desc)`            -> CVAR_STRING
  - `Cvar_RegisterEnum (name, strings[], idx, flags, desc)`  -> CVAR_ENUM
  - also `Vec2/Vec3/Vec4/Color`
- All funnel into `static Cvar_Register()` -> `Cvar_RegisterNew()`.
- cvar storage is engine memory at fixed `iw3mp.exe` addresses
  (`cvar_storage = 0xCBAB808`, `numCvars = 0xCBA73F8`, hash table, etc.).
  CoD4x re-implements registration but writes into the engine's table.

### Available types

`CVAR_BOOL, CVAR_INT, CVAR_FLOAT, CVAR_STRING, CVAR_ENUM` (+ VEC2/3/4,
COLOR). For our controller settings:
- enable/disable        -> `CVAR_BOOL`
- look / ADS sensitivity-> `CVAR_FLOAT` (with min/max domain)
- deadzone per stick    -> `CVAR_FLOAT`
- invert Y              -> `CVAR_BOOL`
- acceleration curve    -> `CVAR_ENUM` (named presets) or `CVAR_FLOAT`

### How to make it save to config_mp.cfg

`q_shared.h:1160`: `#define CVAR_ARCHIVE 1  // set to cause it to be saved`.
Pass `CVAR_ARCHIVE` in the flags argument -> the engine writes the cvar
into `config_mp.cfg` automatically on config save. No extra code.

Confirmed live examples:
- `win_input.c` `IN_Init()`: `raw_input` and `in_mouse` use
  `CVAR_LATCH | CVAR_ARCHIVE`.
- `cg_main.c:1121`: `zoom_sensitivity_ratio` =
  `Cvar_RegisterFloat("zoom_sensitivity_ratio", 1.0, 0.0, 10.0, CVAR_ARCHIVE, ...)`
  -- this is essentially the ADS-sensitivity cvar already; a direct model.

### Conclusion for Stage A

Register `cl_gamepad_*` cvars with `CVAR_ARCHIVE`, each with sane
min/max (the domain gives free validation + console help text). The
natural place is an input-init function (see Q2: `IN_StartupGamepads()`).

---

## Q2 — Input pipeline

### Two parallel input streams

**(a) Discrete events (buttons / keys)**
- `Com_QueueEvent(time, SE_KEY, keynum, down, 0, NULL)` pushes a key
  event onto the engine event queue.
- The key system maps `keynum` -> `playerKeys.keys[keynum].binding`
  string -> a command (`+attack`, `+reload`, ...).
- `win_input.c` `IN_MouseEvent()` already does exactly this for mouse
  buttons: `Com_QueueEvent(..., SE_KEY, K_MOUSE1 + button, down, ...)`.
- `keycodes.h` already defines **`K_JOY1` .. `K_JOY32`** (and `K_AUX1..16`).
  Joystick buttons can be queued as `SE_KEY` events with those codes and
  bound through the normal bind system -- no engine change needed.

**(b) Analog values (look / movement)**
- Mouse motion: `win_input.c` `IN_MouseMove()` ->
  `CL_MouseEvent(x, y, dx, dy)` (`cl_main.c:9416`).
  `CL_MouseEvent` accumulates `cl.mouseDx[]/mouseDy[]`; the engine's
  usercmd builder later turns that into a view-angle delta.
- `CL_FinishMove(usercmd_t *cmd)` (`cl_main.c:9474`) finalizes a usercmd:
  writes `cmd->angles[]` from `client->viewangles[]`, OR's `cmd->buttons`,
  sets weapon fields.
- `usercmd_t` (`q_shared.h:1897`) has `signed char forwardmove,
  rightmove, upmove` -- analog movement, range ~-127..127.

### Per-frame hook point

`win_input.c` `IN_Frame()` runs every frame and calls `IN_MouseMove()`.
**It already contains commented-out gamepad scaffolding:**
- `IN_Startup()` line ~305: `//IN_StartupGamepads();`
- `IN_Frame()` line ~458: `// IN_GamepadsMove();`
- file header comment: "Added extended DirectInput code to support
  external controllers."

So the original RTCW/Q3 lineage had joystick support; CoD4x stubbed it
out. The hook points are pre-marked.

### Best XInput injection point (parallel, non-breaking)

Inject alongside the existing mouse/keyboard paths -- never replace them:

1. **Right stick -> look:** feed synthetic `dx/dy` into the SAME
   `CL_MouseEvent(x, y, dx, dy)`. Sensitivity, ADS scaling and the
   usercmd angle math all already flow from there -> zero risk to mouse.
2. **Buttons -> actions:** `Com_QueueEvent(time, SE_KEY, K_JOY1.., down,
   0, NULL)`. Bound via the standard bind system, fully parallel to the
   keyboard.
3. **Left stick -> movement:** keyboard movement is binary; true analog
   needs `forwardmove/rightmove` set directly. Cleanest = hook the
   usercmd build / `CL_FinishMove` and add the stick contribution
   (additive with keyboard). MVP shortcut: queue synthetic key events
   for digital 8-way movement first, do analog later.
4. **Init:** revive `IN_StartupGamepads()` -- open XInput, register the
   `cl_gamepad_*` cvars there.

Driver choice: **XInput** (`xinput9_1_0` for max Windows compatibility)
-- standard for Xbox-style pads, far simpler than the old DirectInput
code that was commented out.

---

## Q3 — Menu system (for Stage B, later)

### Menu language

- Menus are `.menu` text scripts: `menudef { ... }` blocks.
- Parser in `src/ui_main.c`: `Menu_New()`, the `"menudef"` keyword
  handler (~line 1008), `Menus_FindName()`, `Menu_Open/Close()`,
  `Menus_Open/Close()`, `Menus_AddMenu()`, plus the menu stack.
- `ui_main.c:417` already branches on a `"Controls"` token.

### Where the current Controls menu lives

The `.menu` asset files are NOT in the source tree -- they are packed
inside the base-game archives:
`game-test\main\iw_00.iwd .. iw_13.iwd` (each ~160 MB), as
`ui_mp/controls_*.menu` (e.g. `controls_multi.menu`).
Reading them requires extracting an iwd (a zip). **Deferred** -- not
needed until Stage B.

### Adding a "Gamepad" page without breaking anything

- Author a new `.menu` file with its own `menudef`, ship it inside a
  mod iwd with load-order priority (the sandbox already loads
  `fake-appdata\CallofDuty4MW\main\jcod4x_00.iwd`).
- Add one button to the existing controls menu that does
  `open gamepad_menu`.
- The gamepad page's slider / list items bind directly to the
  `cl_gamepad_*` cvars (standard `dvarFloat`/`dvarInt`/`dvarEnum`
  menu-item bindings) -> Stage A and Stage B share the same cvars.
- Additive only: never modify a base `iw_*.iwd`.

---

## Recommendation — where the actual code starts (Stage A)

1. New file `src/gamepad.c` in `CoD4x_Client_pub`, added to
   `CMakeLists.txt` source list; link `xinput9_1_0`.
2. `IN_StartupGamepads()` -- register every `cl_gamepad_*` cvar with
   `CVAR_ARCHIVE`, open XInput. Called from `IN_Startup()` (uncomment
   `win_input.c` line ~305).
3. `IN_GamepadsMove()` -- once per frame: poll XInput, apply
   deadzone/sensitivity/curve from the cvars, then:
   - right stick -> `CL_MouseEvent(dx, dy)`
   - buttons     -> `Com_QueueEvent(SE_KEY, K_JOY*, down)`
   Called from `IN_Frame()` (uncomment `win_input.c` line ~458).
4. Left-stick analog movement: second iteration -- hook the usercmd
   build to add `forwardmove/rightmove`.

This keeps every change inside `CoD4x_Client_pub` (the cdylib we already
build), touches no engine binary, and runs strictly parallel to the
keyboard/mouse paths -- nothing existing breaks.

### Known gaps / honesty notes

- The actual `.menu` files were not opened (packed in 160 MB iwds);
  Q3 specifics will be confirmed when Stage B begins.
- `forwardmove/rightmove` analog injection point (`CL_FinishMove` vs the
  engine's usercmd builder) needs one more look once Stage A look/buttons
  work -- the builder itself lives in `iw3mp.exe`, not the source.
