# NK — Usage

This document covers the three ways to run NK — the Windows desktop client, the Android client, and command-line dispatch straight to GitHub Actions — including what every workflow field does, where the token is kept, where artifacts land, and how to read the failures you will actually hit.

## Prerequisites

| Requirement | Value |
|---|---|
| Access | Private repository `3495866893/nk-obf`; a GitHub token with `repo` + `workflow` scope |
| Client (desktop) | Windows x64; `NKBuild-1.0-win64.exe` (13.35 MB) is self-contained |
| Client (Android) | `minSdk 26`; `NKBuild-mobile-1.0-kami.apk` (9.98 MB) |
| Build host | GitHub-hosted `ubuntu-latest` runner; nothing is compiled locally |
| Compiler (only if you build the plugin yourself) | LLVM 21 (`LLVM_VER: "21.1.8"` in CI), NDK r29 |

## Workflow 1 — Desktop

The desktop client (`tools/NKBuild.py`, Python 3 + CustomTkinter, about 1000 lines, packaged with `pyinstaller --onefile --noconsole` — numpy/scipy/matplotlib/pandas/yaml/PIL excluded, 13.99 MB packed) is a thin front end over the same workflow the other two clients call.

1. **Launch** the executable. Four cards appear down the left, the log pane and progress bar on the right.
2. **Source card** — pick the local project directory. The scan separates two file sets: *compile units* (`.c` / `.cpp`) and *files to upload* (headers, resources, anything the units include). Headers are not optional: a missing `imgui.h` is a hard `file not found` on the runner.
3. **Project·artifact card** — repository, branch, source directory name, source-file list, output name (`<out_name>.cfg`).
4. **Anti-debug card** — `anti_debug` (link `nk_ad_extra.o`), `skip_smoke`, `rebuild_clang`.
5. **Passes** — tick any of the 16; the client writes `ci/ui-<proj>.toml` locally and uploads it with the sources.
6. **Start** — the client creates blobs, builds a tree, commits, dispatches `build-obf.yml`, polls the run, shows incremental logs and an ETA, and downloads the artifact.

| Behavior | Detail |
|---|---|
| Blob upload | Four-step backoff retry per blob; 1 / 2 / 5 / 20 MB were all accepted with HTTP 201; the hard limit is 100 MB per blob |
| Included-file filtering | Files that are `#include`d by another unit are dropped from the compile list to avoid duplicate symbols (`res/cJSON.c` was the original offender) |
| Output directory | Seven-level fallback: next to the executable → Desktop → Documents → `~` → `LOCALAPPDATA` → `LocalLow` → `TEMP` |
| Log pane | Incremental; a failure names the pass or the compile unit |
| Controls | Progress bar, ETA, abort, open-output-folder |

## Workflow 2 — Android

Package `com.nk.nkbuild`, Kotlin + Jetpack Compose, signed with the fixed key `android/keystore/nkbuild.p12` (PKCS12, self-signed, 30 years) so upgrades install over previous builds.

1. **Open the app**, enter the license key on the gate screen if it is not yet stored.
2. **Pick the source directory** with the system picker.
3. **Precheck** — the scan lists dangerous files before upload (>70 MB fatal, >45 MB likely to fail, spaces or non-ASCII in compile-unit names fatal, images/fonts/documents only slow you down, >60 units risks rate limiting, >120 MB total is slow).
4. **Passes** — 16 toggles; the TOML is generated locally and uploaded.
5. **Build** — the request goes to the same workflow; `BuildService` keeps it alive in the foreground.
6. **Collect** — the artifact lands in `Download/NKBuild` with a one-tap install.

| Behavior | Detail |
|---|---|
| Background survival | Foreground service `BuildService` plus `runId` resume: if the app is killed, re-entering continues polling and downloading the same run |
| Task history | A card per build: project·artifact, run id, timestamp, downloaded flag; tapping re-downloads |
| UI | Liquid-glass bottom navigation (Haze), per-page skin artwork with `Crossfade`, light theme (`#F6EFF4` background, `#D2557F` accent) |
| Feedback | Four button sounds (single `SoundPool` stream, so they never stack), haptics, auto-dismissing toast top-left |
| Version display | `versionName` stays `1.0`; only `versionCode` increments |

## Workflow 3 — CI only (command line)

No client at all. Dispatch the workflow directly:

```bash
gh workflow run build-obf.yml \
  -f src_dir=proj \
  -f src_files="mian.cpp defense.c" \
  -f config_file=ci/tuned.toml \
  -f anti_debug=true \
  -f skip_smoke=true \
  -f out_name=lucifer
```

### Workflow fields

| Field | Default | Meaning |
|---|---|---|
| `src_dir` | `proj` | Source directory inside the repository |
| `src_files` | `mian.cpp defense.c` | Space-separated units, relative to `src_dir` |
| `presets` | `max mid` | Preset fallback order tried when a build fails |
| `defense_level` | `3` | Strength passed to `defense.c` (`0`–`3`) |
| `config_file` | `ci/tuned.toml` | TOML actually used for the build |
| `cfg_list` | empty | Run several configs in one dispatch, `name:toml` separated by spaces |
| `cfg_only` | empty | Use only this TOML, skip the preset ladder (IR-level anti-debug only takes effect this way) |
| `anti_debug` | `true` | Link the runtime watchdog (`nk_ad`) |
| `rebuild_clang` | `false` | Rebuild LLVM from source when the cache is unusable; this is the slow path, and it is only needed when Release `llvm-build-21.1.8` cannot be restored |
| `skip_smoke` | `true` | Skip the smoke test; saves roughly 4 minutes |
| `out_name` | `lucifer` | Artifact name prefix |

The workflow also runs on `push` to `proj/**` or to `build-obf.yml` itself. Environment pinned in the file: `LLVM_VER: "21.1.8"`, `WORK: /mnt/work`, `NDK_DIR: /mnt/work/android-ndk-r29`, `timeout-minutes: 350`.

### Building the plugin locally instead

```bash
export LLVM_DIR=$LLVM_BUILD/lib/cmake/llvm
bash ci/build-nk.sh                      # produces libNk.so

export NK=1
export NK_CONFIG=$PWD/ci/tuned.toml
bash ci/build-lucifer.sh <NDK_DIR> <PLUGIN_BUILD_DIR> <out_name>
```

The pre-rename `ENSIA` / `ENSIA_CONFIG` names still work as aliases. `ci/build-lucifer.sh` locates `ld.lld`, the sysroot and the aarch64 runtime, collects include paths, compiles unit by unit with `-c`, links `libc++_static` / `libc++abi` / `libunwind`, appends `nk_ad_extra.o` when anti-debug is on, then `llvm-strip`s the result.

## Where the token lives

| Client | Entry point | Storage |
|---|---|---|
| Desktop | Source card, GitHub token field | Local settings file next to the client (`%APPDATA%\NKBuild\config.json` for the packaged build) |
| Android | First-run form, editable in settings | `SharedPreferences` key `token` (`Prefs.token`), plus `kami` for the license key |
| CI | `gh auth login` or `GH_TOKEN` | `gh` credential store |

The token is only ever sent to `api.github.com` as an `Authorization` header; it is not embedded in the artifact, the TOML, or the commit.

## Where artifacts land

| Stage | Location |
|---|---|
| Runner | `<out_name>.cfg` and the `nk-toolchain.tar.gz` bundle, uploaded to the **Release** `nk-builds`, not to workflow artifacts |
| Workflow artifact | Only small final artifacts, `retention-days: 3` |
| Desktop client | `<out>.cfg` in the first writable directory of the seven-level fallback chain |
| Android client | `Download/NKBuild/` |
| Recovery | A run's artifact stays reachable from the run page and from the task-history card; re-download works without rebuilding |

## Reading the common failures

| Symptom | Root cause | What to do |
|---|---|---|
| `The runner has received a shutdown signal` / exit **143** | SIGTERM issued under memory pressure — `timeout-minutes` (350) and `concurrency` were ruled out, and so was `systemd-oomd` | This is the big-unit path: keep `flattening` off big units, confirm the tiered degradation actually engaged, check `mem_avail` in the resource log |
| Exit **137** | Real OOM kill, distinct from 143 | Same as above; check `mem_avail` in the resource curve |
| `Illegal instruction` / SIGILL right after start | `flattening` marked a switch default `unreachable`; `bcf` and `split_blocks` made that branch reachable; the backend emitted `brk #0xf000` / `udf` | Skip `flattening` for the unit; `-fno-asynchronous-unwind-tables -fno-unwind-tables` are already in the base flags |
| `Undefined temporary symbol .L_v*` at link | String-encryption globals inherited a thread-local model on a non-empty Android triple | Fixed in the plugin; if it reappears, the global's thread-local mode was overridden again |
| `relocation refers to a discarded section: .text.startup` | COMDAT / `linkonce_odr` function split into a new section, original discarded, relocations still pointing at it | Fixed by the COMDAT skip in `Utils.cpp toObfuscate()` |
| App dies silently on a real device, no log | Watchdog hit → `_exit(97)` | `NK_AD_DEBUG=1`, then read `/data/local/tmp/nk_ad.log` |
| App hangs before any window appears | `string_encryption` with `anti_dump = false` — hangs in a global constructor | Set `anti_dump = true` or disable the pass outright |
| `Failed to CreateArtifact: Artifact storage quota has been hit` | 500 MB quota exhausted (once 289 artifacts / 24.4 GiB) | Artifacts are no longer used for toolchains; `cleanup.yml` plus `retention-days: 3` |
| `duplicate symbol` for cJSON or similar | An included `.c`/`.cpp` was also compiled as its own unit | The clients filter included units; drop it from `src_files` by hand |
| `imgui.h: file not found` | Headers were not uploaded, only units | Upload the whole directory; the desktop scan's second set exists for this |
| Upload stalls on a large file | Blob above ~20 MB, near the 100 MB ceiling | Reduce the file; 70 MB is already refused by precheck |
| License gate: `code=179` | Server answers inactive accounts on the recharge page | The v1.0 client has no account system; use the key-only flow |
| License gate: "key already used" | A key binds to one device | Unbind first (`XlVerify.unbind`), or look the key up again |
| Wrong expiry date shown | The server returns `vip` inside `msg` as a string; `4102243200` is epoch **seconds** | Parsed as seconds since v1.0 |
| New APK will not install over the old one | Different debug keystore each build | Always sign with `android/keystore/nkbuild.p12` |

### First-line diagnostics

```bash
# plugin actually rewriting IR?
NK=1 NK_CONFIG=$PWD/ci/tuned.toml clang++ -fpass-plugin=$PWD/libNk.so -O2 -c a.cpp -o a.o

# did the artifact keep the trap sentinels?
llvm-objdump -d out.cfg | grep -cE '\bbrk\b'   # expect 0
llvm-objdump -d out.cfg | grep -cE '\budf\b'   # expect 0

# watchdog verdict on device
adb shell "NK_AD_DEBUG=1 /data/local/tmp/your_binary"
adb shell "cat /data/local/tmp/nk_ad.log"
```

A healthy device run logs `TracerPid=0 wd=0 hooked=0`. Attaching `nk_spawn` (the ptrace attacher) must produce `exit 97`.
