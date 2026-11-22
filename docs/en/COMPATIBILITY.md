# Compatibility and Known Limits

What NK runs on, what has been verified on real devices, and where it breaks.

## Requirements

| Item | Requirement | Notes |
|---|---|---|
| LLVM | 21.x | The plugin ships as `libNk.so` loaded through `-fpass-plugin`; its ABI is tied to the LLVM major version |
| NDK | r29 | Supplies clang 21, lld and the sysroot; CI restores it from the NDK cache, a miss costs one download |
| Target ABI | aarch64-linux-android | The anti-debug paths contain AArch64 inline assembly; other architectures are untested |
| `--target` API | android21+ | The build script pins `--target=aarch64-linux-android21` |
| Android | minSdk 26 | Both the GUI and the produced binaries |
| CI | GitHub Actions (private repo) | Cached end-to-end build is 1.8 min (was 17.6 min); the job cap is `timeout-minutes: 350` |
| Desktop | Windows x64 | Single-file executable, no Python needed |
| Build host memory | 16 GB measured | Large TUs with all passes drove `mem_avail` down to 119–398 MB on a 16 GB runner with swap already full |

## Verified on hardware

| Device | OS | Result |
|---|---|---|
| realme GT5 Pro (`RMX3888`) | Android 16 / SDK 36 | Obfuscated build starts normally and prints its own UI; `NK_AD_DEBUG=1` logs `TracerPid=0 wd=0 hooked=0` (no false positives); `exit 97` when attached by `nk_spawn` |
| realme (`RMX3300`) | Android 15 / SDK 35, KernelSU | Same behavior; under the root domain (`u:r:ksu:s0`) even the first `ptrace` call returns EPERM, which is why the "foreign tracer only" rule is required |
| 8-pass build | — | 100 consecutive launches, zero hangs |
| Startup time | — | 2–4 s, dominated by the sample app's own 3-second `select` timeout in `httpget_compat`, not by obfuscation |

## Known limits

1. **Large TUs skip `flattening`**: translation units larger than 200 KB of source or 800 KB preprocessed use `ci/nk-nofold.toml` (7 passes). See stage D in [CHANGELOG](CHANGELOG.md): a 17-file build with `flattening` on large units carried 12 `brk` plus 8 `udf` sites and died with SIGILL, after the backend had already driven `mem_avail` to 119–398 MB.
2. **`anti_dump=false` is unusable**: measured to hang inside the global constructor (`__dl_soinfo::call_constructors`) — one thread pegged at 100% CPU with no output. Default is `anti_dump=true`.
3. **`anti_hooking` is off**: its integrity baseline never matches in this environment (every CI build has a different layout), so enabling it means guaranteed false positives.
4. **`anti_class_dump` / `objc` are not selectable**: only meaningful for Objective-C / Java class dumps, useless for native Android projects.
5. **Obfuscation is not unbreakable**: it raises the cost (strings unreadable, control flow flattened, constants split), but a determined analyst with time can still recover logic.
6. **The watchdog trusts `/proc` and raw `svc`**: on ROMs that rewrite `/proc/self/maps`, the checks fail open — lower protection, never a false kill.
7. **Single private repo**: caches, releases and artifacts all live there; switching repos means updating `%APPDATA%\NKBuild\config.json` and the Android settings screen.

## Failure cheat-sheet

| Message | Meaning | Action |
|---|---|---|
| `exit code 143` + `runner has received a shutdown signal` | Job killed by SIGTERM under memory pressure | Large units degrade automatically; if it still fails, drop `bcf` as well |
| `Illegal instruction` | The binary contains `brk`/`udf` sentinels | Scan `PT_LOAD\|PF_X` segments; route that TU through the large-TU policy |
| `Undefined temporary symbol .L_v…` | Historical TLS bug in `string_encryption` | Fixed in the plugin; make sure `libNk.so` comes from the current kit |
| `duplicate symbol: cJSON_*` | A `.c` file is both compiled and `#include`d | The GUI strips included sources; keep `res/cJSON.c` out of `SRC_FILES` |
| `Artifact storage quota has been hit` | Actions artifact quota exhausted | Purge old artifacts; binaries go to Releases, artifacts live 3 days |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | Signature changed | `android/keystore/nkbuild.p12` is pinned now; if it still happens, uninstall first |

## Artifact acceptance gates

A build is treated as verified only when every row passes. These are the checks that caught each historical regression, in the order they are run.

| Gate | Command | Expected |
|---|---|---|
| Trap sentinels | `llvm-objdump -d out.cfg \| grep -cE '\bbrk\b'` | `0` |
| Undefined instructions | `llvm-objdump -d out.cfg \| grep -cE '\budf\b'` | `0` |
| Code section | `llvm-size -A out.cfg` | 16,086,400 B on the reference build |
| Artifact size | `stat -c %s out.cfg` | 21,145,056 B on the reference build |
| Artifact hash | `sha256sum out.cfg` | `6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34` on the reference build |
| Plaintext domain | `strings out.cfg \| grep -c 'yz.dujiuyue.cn'` | `0` |
| Plaintext key marker | `strings out.cfg \| grep -c 'kami'` | `0` |
| Watchdog quiet | `NK_AD_DEBUG=1 ./out.cfg` on device | `TracerPid=0 wd=0 hooked=0` |
| Watchdog armed | attach `nk_spawn` (ptrace attacher) | `exit 97`, silent |

The size, hash and section figures describe one specific build — `build lucifer` 41 s in run `45041678195`. Another source tree produces different values; use them as a shape reference, not as a checksum to match.

## Environment assumptions

| Assumption | If it does not hold |
|---|---|
| `/proc/self/maps`, `/proc/self/task`, `/proc/net/*` are readable | Checks fail open: less protection, no false kill |
| ART is the runtime and the JNI table points into `libart` | `nk_jni_table_ok` records one soft hit; nothing is killed |
| `perf_event_open` is permitted for the process | Hardware-breakpoint slot detection silently skips |
| The NDK r29 bionic `libc` is in use | Prologue and hash baselines are re-sampled at the first deep check instead of trusting a stale snapshot |
| The repository stays private and CI stays on GitHub-hosted runners | Cache keys, Release targets and token scopes must be re-pointed |
| `minSdk 26` is respected | The Android client itself installs, but older devices are outside the validated range |

## Pre-delivery checklist

1. Compile unit by unit with `-c`; never hand a 17-file project to one clang invocation.
2. Measure each unit with `clang++ -E -P unit.cpp | wc -c`; route anything large through the `nofold` tier before trying the full set.
3. Confirm `anti_dump = true` in every configuration that enables `string_encryption`.
4. Confirm `flattening` is disabled in the configuration used for large units.
5. Scan the artifact for `brk` and `udf`; both must be `0`.
6. Run the artifact on the test device with `NK_AD_DEBUG=1` and check for `TracerPid=0 wd=0 hooked=0`.
7. Attach `nk_spawn` once and confirm `exit 97`.
8. Only then publish to Release `nk-builds`; workflow artifacts expire after 3 days and are not a distribution channel.
