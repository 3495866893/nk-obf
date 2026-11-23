# NK (`nk-obf`) — Overview

This page is the entry point for NK: what the project is, which three product lines live in the repository, how the tree is laid out, and the four steps that take a source directory to a hardened binary.

## What NK is

NK is an IR-level code obfuscation plugin for **LLVM 21**. It is built as a shared object named `libNk.so` (CMake target `Nk`) and loaded into clang with `-fpass-plugin`; paired with the **Android NDK r29** toolchain it turns an Android native project into an aarch64 binary whose reverse-engineering cost is deliberately high.

The plugin rewrites LLVM IR — it never parses C or C++ text — so macro expansion, template instantiation, coding style and language standard do not affect it. A second, independent piece ships with it: the runtime watchdog `ci/nk_ad.c`, which is compiled separately and linked into the target binary. It is the only part of the product that runs at application startup.

The repository is private (`3495866893/nk-obf`).

## The three product lines

All three live in the same repository and share one pipeline: pick a directory, tick the passes, upload, trigger GitHub Actions, download the artifact. None of them needs a local clang or NDK.

| # | Line | Entry point | Stack | What it does |
|---|---|---|---|---|
| 1 | LLVM pass plugin | `src/` → `libNk.so` (target `Nk`) | C++ on LLVM 21, CMake | 16 module passes over IR; TOML switch per pass; seed-driven reproducibility |
| 2 | Desktop GUI | `tools/NKBuild.py` | Python 3 + CustomTkinter (~1000 lines) | Four cards (source / project·artifact / anti-debug / build) + log pane, progress bar, ETA, abort, open-output |
| 3 | Android GUI | `android/` (package `com.nk.nkbuild`) | Kotlin + Jetpack Compose | Same flow on the phone; 16 pass toggles, local TOML upload, `Download/NKBuild`, one-tap install |

A fourth, smaller component is not a product line but a runtime module: **`ci/nk_ad.c`** — anti-debug / anti-injection watchdog, compiled on its own, obfuscated with `string_encryption` only, linked as `nk_ad_extra.o`.

## Repository layout

| Path | Contents | Notes |
|---|---|---|
| `src/` | Plugin sources, 79 files / 2.5 MB | One translation unit per pass plus `Utils` |
| `ci/build-nk.sh` | Builds `libNk.so` from an LLVM 21 build tree | 1.7 KB |
| `ci/build-lucifer.sh` | NDK r29 cross-compile driver for arbitrary projects | 13 KB; collects `-I`, links static libc++, appends `nk_ad_extra.o` |
| `ci/nk_ad.c` | Runtime watchdog shell + eight `.inc` parts under `ci/nk_ad_parts/` | Separate compile; `string_encryption` only; entry symbol randomised per build |
| `ci/*.toml` | Pass presets: `plain`, `fast`, `tuned`, `tuned-noadb`, `max-*`, `nk-nofold`, `nk-ir`, per-pass probes | `nk-nofold.toml` = 7 passes, skips `flattening` |
| `ci/brand.txt` | Vendor string injected into artifacts | `NK-Ollvm[QQ:3495866893]…` |
| `tools/NKBuild.py` | Desktop GUI | Python 3 + CustomTkinter |
| `tools/xlverify.py` | License-key verifier: SM4-CBC + HMAC-SM3 envelope, pure Python | Used by the desktop GUI only |
| `android/` | Kotlin/Compose client, `BuildService`, `Precheck`, `XlVerify`, `Prefs` | `android/keystore/nkbuild.p12` is the fixed signing key |
| `.github/workflows/` | `build-obf.yml`, `build-plugin.yml`, `build-apk.yml`, `cleanup.yml` | `build-obf.yml` is the main pipeline |
| `docs/` | Documentation (Chinese originals, this English set) | 7 documents |
| `kit.tar.gz` | Plugin kit tarball; its `sha256[:12]` is the cache key | 527 KB |
| `LICENSE` | MIT | — |

The tree was cleaned before publication: `proj/` and `projects/myproj/` (37.4 MiB, of which `font.h` alone was 20 MB) and `projects/路西法/` were removed, taking the repository from **410 files / 63.3 MiB** to **294 files / 24.9 MiB**.

## Four-step quick start

1. **Get a client.** Windows: `NKBuild-1.0.2-win64.exe` (13.35 MB) from Release v1.0.2. Android: `NKBuild-mobile-1.0-kami.apk` (9.98 MB). Or drive the workflow from the command line — see [USAGE.md](USAGE.md).
2. **Point it at a source directory.** The client scans compile units, headers and resources separately; on Android the precheck page flags files that will break the build (>70 MB, spaces or non-ASCII in unit filenames, >60 compile units) before anything is uploaded.
3. **Choose passes.** Either tick the 16 toggles or load a TOML; the client generates `ci/ui-<proj>.toml` and uploads it with the sources.
4. **Build and collect.** Dispatch `build-obf.yml`, wait for `build lucifer`, download `<out>.cfg`; the desktop client drops it in its resolved output directory, the Android client in `Download/NKBuild` with a one-tap install.

```bash
gh workflow run build-obf.yml \
  -f src_dir=proj \
  -f src_files="mian.cpp defense.c" \
  -f config_file=ci/tuned.toml \
  -f anti_debug=true \
  -f out_name=lucifer
```

`src_files` defaults to the literal string `mian.cpp defense.c` in `build-obf.yml`, so override it for any real project.

## At a glance

| Fact | Value |
|---|---|
| Compiler / plugin host | LLVM 21 (CI pins `21.1.8`) |
| Android toolchain | NDK r29, `minSdk 26` |
| Passes | 16 total: 8 core on by default, 5 experimental off, 3 disabled (not selectable) |
| Runtime watchdog | `ci/nk_ad.c`, generation v12.2 |
| End-to-end CI | 17.6 min → **1.8 min** after caching |
| Last reference build | run `45041678195`, `build lucifer` **41 s**, artifact **21,145,056 B** |
| Artifact sha256 | `6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34` |
| Code section | 16,086,400 B, `brk 0 / udf 0` |
| Repository size | 294 files / 24.9 MiB |
| Current release | v1.0.2 |

## Documentation index

| Document | Covers |
|---|---|
| [FEATURES.md](FEATURES.md) | The 16 passes as a table, the `nk_ad.c` detection list by category, precheck rules |
| [USAGE.md](USAGE.md) | Desktop / Android / CI-only workflows, every workflow field, token, artifacts, failure reading |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Pipeline, pass order over IR, three-tier big-TU degradation, CI caching, resume data model |
| [COMPATIBILITY.md](COMPATIBILITY.md) | Requirements, on-device validation record, known limitations |
| [CHANGELOG.md](CHANGELOG.md) | Stage-by-stage history A–G: symptom → root cause → fix → verification |
| [RELEASES.md](RELEASES.md) | Download entries, license-key system and how to remove it, versioning convention |
| [CREDITS.md](CREDITS.md) | Dependencies, upstream lineage, third-party components |
