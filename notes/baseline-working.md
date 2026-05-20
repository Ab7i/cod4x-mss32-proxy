# Baseline — WORKING sandbox (verified 2026-05-20)

The modified-launcher sandbox boots CoD4x successfully. This file is the
return point: if anything later breaks, restore to the `baseline-working`
git tag and re-deploy the artifacts described here.

## Verification evidence

Launched `iw3mp.exe` from `game-test\` on 2026-05-20 ~19:17. User-confirmed
in-game observations:

- Main menu title shows **"Call of Duty 4 X"** (CoD4x loaded).
- Reported version **21.3**.
- **"Create a Class"** menu tab tagged *new* (CoD4x feature present).
- `iw3mp.exe` exit code **0** (clean exit, no crash).
- No updater dialog appeared (updater code was removed in Phase 3).

No screenshot file was captured — evidence is the user's direct observation.

## Built artifacts (the three DLLs)

| DLL | Size | SHA256 |
|---|---|---|
| mss32.dll (our proxy) | 58,880 | `4A5C181E4EF7D5A3C0FC8E02F9385284E45FEBA441C0DD5D328242ACFF837F60` |
| launcher.dll (our modified) | 404,992 | `639287AFE6B3B1DC846DA45960B4F77FEE469B42FC172D86EDE53D4ADAF70E7F` |
| cod4x_021.dll (our client build) | 2,617,344 | `E3FF3BD24318CCB8D4742343682AA4C81DE7537664B19B28B7152C5C7F6912C4` |

Build outputs at source (before deployment):
- `mss32-proxy\build\mss32.dll`
- `CoD4x_Client_pub\build\bin\cod4x_021.dll`
- `CoD4x-launcher\target\i686-pc-windows-gnu\release\launcher.dll`

## Where every file of ours lives (full paths)

Source repos:
- `D:\Cod4Project\CoD4x_Client_pub\`  — cod4x_021.dll source (git, fork Ab7i)
- `D:\Cod4Project\CoD4x-launcher\`    — launcher source (git, fork Ab7i)
- `D:\Cod4Project\mss32-proxy\`       — our mss32 proxy source (NOT yet a git repo)

Deployed sandbox (`D:\Cod4Project\game-test\`):
- `game-test\iw3mp.exe`        — host exe (3,330,048 bytes, stock v1.7)
- `game-test\mss32.dll`        — OUR proxy
- `game-test\miles32.dll`      — original Miles, unchanged (434,688 bytes)
- `game-test\fake-appdata\CallofDuty4MW\bin\launcher.dll`              — OUR modified launcher
- `game-test\fake-appdata\CallofDuty4MW\bin\launcher.dll.official.bak` — backup of the official launcher (2,165,248 bytes)
- `game-test\fake-appdata\CallofDuty4MW\bin\cod4x_021\cod4x_021.dll`   — OUR client build
- `game-test\fake-appdata\CallofDuty4MW\main\*`  — mod assets (jcod4x_00.iwd ...)
- `game-test\fake-appdata\CallofDuty4MW\zone\*`  — patch assets (cod4x_patchv2.ff ...)
- `game-test\players\profiles\Alpha\config_mp.cfg` — where the game writes
  player config (CoD4x fs_homepath = game folder, NOT fake-appdata)

Helper scripts at project root:
- `setup-env.bat`        — per-session PATH for the portable toolchain
- `launch-test.bat`      — game launch wrapper (note: mangles under
  non-interactive background invocation; launch iw3mp.exe directly instead)
- `firewall-block.ps1` / `firewall-unblock.ps1` — outbound block on the
  sandbox iw3mp.exe (rule active)

## How the sandbox works — full loader chain

```
iw3mp.exe (game-test\)
  └─ PE import: mss32.dll  → loads game-test\mss32.dll  (OUR proxy)
       ├─ DllMain: GetModuleFileNameW → resolve own dir = game-test\
       ├─ SetEnvironmentVariableW("LOCALAPPDATA",
       │      "D:\Cod4Project\game-test\fake-appdata")
       ├─ forward 340 Miles exports → miles32.dll  (via exports.def)
       ├─ LoadLibraryW(absolute path to fake-appdata\...\bin\launcher.dll)
       └─ GetProcAddress("StartLauncher") → call
            └─ launcher.dll!StartLauncher  (OUR modified launcher)
                 ├─ set_current_directory(install dir)
                 ├─ iw3mp::is_pure() + is_large_address_aware()  — local checks
                 ├─ miles32::load_module() — forward the Miles import table
                 └─ run_thread → cod4x::run()
                      ├─ read LOCALAPPDATA env var (= fake-appdata)  ← reversed
                      │      priority: env var first, SHGetKnownFolderPath only
                      │      as fallback
                      ├─ glob  fake-appdata\CallofDuty4MW\bin\cod4x_*\
                      │      → cod4x_021\cod4x_021.dll
                      ├─ LoadLibrary(cod4x_021.dll)  (OUR build)
                      └─ GetProcAddress("WinMain@16") → call → game runs
```

No network at any step. No hash/integrity check. No updater dialog.
Firewall outbound-block on the sandbox iw3mp.exe is a belt-and-braces guard.

## Toolchain (portable, in tools\)

Rust 1.95.0 i686-pc-windows-gnu, MinGW gcc 13.2.0, CMake 3.31.5,
NASM 2.16.03. Activated per-session via `setup-env.bat`.

## Next Phase Goals

### Final project goal

Native controller support that is configured **from inside the game**,
the same way modern Call of Duty does it:

- A controller settings list in the **Controls menu** of the main menu.
- Adjustable settings:
  - enable / disable
  - look sensitivity
  - ADS (aim-down-sights) sensitivity
  - deadzone, per stick
  - invert Y axis
  - acceleration curve
  - (aim assist — later)
- Every setting is backed by a **cvar** (standard engine variable).

### Two-stage implementation plan

- **Stage A (developer-friendly):** add the cvars only. Configured via
  the console (`~` key). cvar naming scheme: `cl_gamepad_*` — consistent
  with the original CoD4 cvar naming convention.
- **Stage B (user-friendly):** add a UI page in the Controls menu. It
  binds to the very same cvars defined in Stage A.

### Exploration phase (next session)

Reverse-engineer three subsystems inside `cod4x_021.dll`:

1. **cvar system** — how cvars are registered and how they are read.
2. **input pipeline** — how keyboard/mouse input is translated into game
   actions (this is where XInput will be injected).
3. **menu system** — needed later for Stage B.

## Open item

UCRT vs msvcrt linkage of our launcher.dll — deferred to the PR stage
(see phase2-report.md / phase3-report.md).
