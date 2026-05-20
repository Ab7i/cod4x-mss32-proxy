# Phase 2 report — Plan B (modified launcher build)

Status: **COMPLETE**. Steps 4–9 done. Approved by user.

## Context

Folder renamed `D:\Cod4 Project` → `D:\Cod4Project` (removed the space).
The space had broken the first `cargo build` because GNU tools (`dlltool`,
`as`) mis-parse paths containing spaces.

## 4. Windows Firewall rule

- Old rule (pointed at `D:\Cod4 Project\game-test\iw3mp.exe`) deleted.
- New rule created via `firewall-block.ps1` run elevated (UAC, exit 0).
- `firewall-block.ps1` made path-agnostic: `$ExePath` is now derived from
  `$PSScriptRoot` instead of hardcoded — survives future folder moves.
- Verified: exactly 1 rule with that name — `Outbound / Block / Enabled`,
  Program = `D:\Cod4Project\game-test\iw3mp.exe`.

## 5. Hardcoded old paths

- `setup-env.bat`: already clean — uses `%~dp0` only.
- Old paths found in **comments only** (no live code) and fixed:
  `mss32-proxy\main.c` (3 lines), `launch-test.bat` (1 line).
  Live code computes its path at runtime via `GetModuleFileNameW`.
- CMake build caches still held old paths — deleted in step 7.

## 6. setup-env.bat verification

- `PROJECT_ROOT = D:\Cod4Project`.
- `rustc 1.95.0` and `cargo 1.95.0` resolve from `D:\Cod4Project\tools\rust\bin\`.
- cmake 3.31.5, mingw gcc 13.2.0, nasm 2.16.03 — all working.

## 7. Clean CMake rebuild

`build\` deleted for both projects, reconfigured + rebuilt (MinGW Makefiles):

| Output | Size | Notes |
|---|---|---|
| `mss32-proxy\build\mss32.dll` | 58,880 bytes | 1 warning: `cast-function-type` on `StartLauncher` call — expected, harmless |
| `CoD4x_Client_pub\build\bin\cod4x_021.dll` | 2,617,344 bytes | 1 warning: `stdcall-fixup` for `Direct3DCreate9` — known, pre-existing |

## 8. Clean Rust rebuild (the real space-in-path test)

- `target\` deleted, `cargo build --release` run with no modifications.
- Result: `Finished release profile [optimized] in 1m 00s` — **0 errors, 0 warnings**.
- **The space-in-path problem is resolved.** Root cause was solely the
  space in `D:\Cod4 Project`.

## 9. launcher.dll comparison

| | Our build | Official |
|---|---|---|
| Path | `target\i686-pc-windows-gnu\release\launcher.dll` | `%LOCALAPPDATA%\CallofDuty4MW\bin\launcher.dll` |
| Size | 2,161,152 bytes | 2,165,248 bytes (diff 4096 = one page) |
| Architecture | pei-i386 ✓ | pei-i386 ✓ |
| Exports | `StartLauncher` only (1) ✓ | `StartLauncher` only (1) ✓ |
| SHA256 | `55DA657C089F7FBB76F44F656143CF556FDAC63B1F1FDEF6AE1FCD9A22B27B40` | `61B43581397D7BE1B6002CC37FAE5D4ED12E88E037550C7053DB312A5A342C08` |
| CRT imports | UCRT (`api-ms-win-crt-*`) | `msvcrt.dll` |

Functionally correct: same architecture, same single required export
(`StartLauncher`). SHA256 differs as expected (different toolchain).

## ⚠️ Open item — UCRT vs msvcrt (deferred to PR stage)

Our build links the modern UCRT (`api-ms-win-crt-*`, `ucrtbase`) while the
official launcher links legacy `msvcrt.dll`. This is a build-environment
difference, not a contract difference, and does NOT affect current testing
(UCRT ships by default on Windows 10/11). **To be addressed when we reach
the PR-submission stage** — the community build may expect msvcrt linkage.
