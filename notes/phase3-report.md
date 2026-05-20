# Phase 3 report — launcher modifications

Status: **COMPLETE & DEPLOYED**. Approved by user.

## Goal

Turn the CoD4x launcher into a "dumb" loader: find cod4x_021.dll in the
sandbox fake-appdata, load it, hijack WinMain — with no network, no hash
check, no updater dialog.

## Files deleted (6 files + 1 directory)

```
launcher\src\launcher\http.rs            (network / curl)
launcher\src\launcher\sha1.rs            (hash check)
launcher\src\launcher\zip.rs             (update archive extraction)
launcher\src\launcher\process.rs         (elevated restart — dead after removal)
launcher\src\launcher\error.rs           (used only by process.rs)
launcher\src\launcher\security_info.rs   (used only by iw3mp::replace_module)
launcher\src\launcher\updater\           (whole directory, 8 files)
```

`process.rs` / `error.rs` / `security_info.rs` were NOT in the user's
original list — they were found dead via dependency-chain analysis.

## Files modified (7)

- `mod.rs` — removed module declarations for the 7 deleted units.
- `Cargo.toml` — removed 9 deps (sha1, hex, curl, nwg, serde_json,
  self-replace, semver, regex, zip); trimmed winapi features 14 -> 10.
- `filesystem.rs` — **reversed LOCALAPPDATA priority**: env var first,
  SHGetKnownFolderPath only as fallback (so the mss32 proxy's env override
  is honoured). Removed dead `is_writable`, `disable_directory_virtualization`,
  `appdata_main_path`, `appdata_zone_path`.
- `entrypoint.rs` — removed the `updater::run_updater` block from `run()`;
  removed auto-fix (`replace_module`) paths from `StartLauncher` — on an
  impure iw3mp or miles32 failure it now shows a message and exits.
- `cod4x.rs` — removed `get_module_version()` (the only `GetCoD4xVersion`
  caller; used only by the updater). Load path unchanged: load DLL ->
  `WinMain@16` hijack.
- `miles32.rs` — kept `load_module` (essential — forwards the Miles import
  table); removed `replace_module` + its error type.
- `iw3mp.rs` — kept purity checks (`is_pure`, `is_large_address_aware`),
  `startup`, WinMain hijack; removed `replace_module`, `make_large_address_aware`.

## Build

First build failed: winapi feature trim was too aggressive — `winbase`
and `winerror` were implicit transitive deps (pulled by removed features
`aclapi`/`securitybaseapi`). Fix: added both explicitly to Cargo.toml
(net winapi features 14 -> 10, now all explicit). No `.rs` change needed.

Second build: `Finished release profile [optimized] in 5.01s` —
**0 errors, 0 warnings**.

## launcher.dll result

| Metric | Value |
|---|---|
| Size | **404,992 bytes** |
| Architecture | pei-i386 |
| Exports | `StartLauncher` only (1) |
| Warnings | none |
| SHA256 | `639287AFE6B3B1DC846DA45960B4F77FEE469B42FC172D86EDE53D4ADAF70E7F` |

### Size comparison

| Build | Size | vs modified |
|---|---|---|
| Modified (Phase 3) | 404,992 bytes | — |
| Unmodified (Phase 2) | 2,161,152 bytes | modified is 1,756,160 bytes smaller (-81.3%) |
| Official | 2,165,248 bytes | modified is 1,760,256 bytes smaller (-81.4%) |

~5.3x smaller — removing curl (static libcurl + TLS), nwg (GUI toolkit),
serde_json, regex and zip accounts for the drop.

## Deployment

- Backup: `fake-appdata\CallofDuty4MW\bin\launcher.dll` (official,
  2,165,248 bytes) copied to `launcher.dll.official.bak` — reversible.
- Deployed: modified `launcher.dll` (404,992 bytes) into
  `game-test\fake-appdata\CallofDuty4MW\bin\launcher.dll`.
- mss32.dll proxy (58,880 bytes), cod4x_021.dll (2,617,344 bytes) and
  miles32.dll were already in place from Phase 2.
- Firewall outbound-block on `game-test\iw3mp.exe` active since Phase 2.

## Open item carried forward

UCRT vs msvcrt linkage difference (see phase2-report.md) — still to be
addressed at the PR-submission stage.
