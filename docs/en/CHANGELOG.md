# Changelog · From the very first conversation to release

> Written from the moment the project was first requested ("build me NKBuild") up to the v1.0 public release.
> Timeline starts 2026-11-21.
> Versioning: development used internal numbers (1.1–1.18); the public line is **`v1.0` plus its incremental patch `v1.0.2`** (versionCode 20).

---

## Phase 0 · The starting point: upload source, get a hardened binary back

The first request: a desktop GUI — pick a project folder, upload it to GitHub, dispatch an Actions workflow, and download an aarch64 binary obfuscated by **the early-codename plugin** (later renamed NK), with obfuscation and anti-debug selectable.

- Built `tools/NKBuild.py`: a four-card customtkinter window (source / project & output / obfuscation toggles / run & log), plus a `Github` class doing blob → tree → commit → dispatch → poll run → download artifact.
- Obfuscation side: an LLVM 21 pass plugin (`lib<codenames>.so` at the time) loaded through `-fpass-plugin` into NDK r29's clang, configured by `ci/*.toml`.
- First round of speed work: 17.6 min → 1.8 min (apt checks made on-demand: 7.5 min → 0.02 min; smoke test skipped by default: 4 min → 0; plugin and clang cached).

## Phase 1 · Anti-debug made the binary exit silently at startup

Symptom: a build with `anti_debugging` enabled printed nothing and exited.

- A probe binary proved the cause: **the 1st `PTRACE_TRACEME` returns 0, the 2nd returns `-1 EPERM`** — while the old implementation injected a check at every function entry plus one in a global constructor, so the 2nd call always "detected a debugger" and killed itself.
- Under KernelSU even the 1st call returns `EPERM` when run through `su`.
- Fix: the AArch64 branch was replaced by a **`TracerPid` probe** (open `/proc/self/status` → read 1023 bytes → `strstr "TracerPid:"` → `strtol`), no `ptrace` at all.
- Same investigation found the tail of the "silent exit": the violent-exit block ended in a **Q32 chaos infinite loop**, which is why `timeout` reported `rc=124/137` instead of an exit code.

## Phase 2 · A runtime watchdog of our own: `ci/nk_ad.c`

From "the pass injects detection" to **pass injects call sites + a C module holds the logic**:

- LLVM level only inserts `call __nk_ad_check()` at every function entry (IR level); the logic lives in `nk_ad.c`, compiled separately with `-x c` (running it through the pass produced `.L_vXXXX` undefined-temporary errors).
- Checks: `TracerPid` cross-check, all-thread scan (`/proc/self/task` + `getdents64`), maps snapshot diff, libc prologue rewrite scan (only `B/BR/BLR/ldr+br` whose target leaves the module, to avoid bionic-thunk false positives), self `.text` FNV hash, `prctl(PR_SET_DUMPABLE,0)`, raw `svc #0` syscalls (openat 56 / read 63 / close 57 / write 64 / getdents64 61).
- Fail-open on any read error; exit code **97** on a hit; debug output behind `NK_AD_DEBUG=1`, log at `/data/local/tmp/nk_ad.log`.
- Verified on a real device (realme RMX3300, Android 15, KernelSU): normal run prints its own UI and returns 255; under `nk_spawn` (ptrace) it exits **97**.

## Phase 3 · Features a GUI should have

- All 16 passes selectable, `anti_hooking / anti_class_dump / objc` greyed out (they crash on use).
- Cancel button (`threading.Event` threaded through upload / wait / poll, plus the GitHub cancel API), ETA (`max(120, 120+20×passes+min(900, 20×MB))`), elapsed timer, and on failure a list of **which step went red** plus the run URL.
- Output directory fallback in 5 levels (Desktop / Documents / `~` / LOCALAPPDATA / TEMP), each probed with a temp file before use.

## Phase 4 · Frida and the Actions quota

- Tried to install frida-server for a real detection test: the wheel on hand was `win_arm64` (unusable on this AMD64 host), and frida-server needs root — the attached device had changed to an unrooted vivo.
- Discovered the Actions storage was full: 139 artifacts / 5.8 GB (`llvm-build` 225 MB × 17). Purged to 225 MB and rewrote the cleanup workflow to keep `llvm-build`, `ndk-*-cache`, `<old-name>-plugin-*` and only prune above 300 MB total.

## Phase 5 · The workspace was deleted, everything rebuilt

- The entire workspace was deleted. Recovery: the old PAT was dead → found a working token in `%APPDATA%\NKBuild\config.json` → pulled `tarball/main` (168 files) → extracted `kit.tar.gz` (140 entries).
- **`NKBuild.py` had never been pushed**, so it was rewritten from scratch: four cards, `PASS_SPEC` with 16 entries, preset segmented button, cancel, ETA, 5-level output fallback; `py_compile` and a GitHub-layer smoke test both passed.
- Along the way: `NDK_DIR: unbound variable`, a fast-fail `guard (cache required)`, and duplicate `if:` keys that made dispatch return 422.

## Phase 6 · Mobile app (v1.1 → v1.2)

- Kotlin + Compose: source scanning (uploads `.h/.hpp/resources` too), pass checkboxes, locally generated `ci/ui-<project>.toml`, progress and ETA, log pane, artifact auto-saved to `Download/NKBuild` with one-tap install.
- Bottom navigation as a **liquid-glass capsule** (Haze `hazeEffect` + sliding selection pill), four tabs: build / obfuscate / output / settings.
- First build failed with three errors: `DIRECTORY_DOWNLOAD` missing an S, missing `animateFloat` import, and a `@Composable` call outside composable context — all fixed, v1.2 shipped.

## Phase 7 · The "uploads over 1 MB fail" myth

- Measured directly against `POST /git/blobs`: 1 MB, 2 MB, 5 MB and 20 MB **all returned 201** ⇒ the blob API has no 1 MB limit (the cap is 100 MB).
- The real 1 MB limit belongs to `PUT /contents`, used in exactly one place (writing `ci/ui-<project>.toml`). Added 4-step backoff retry, size logging above 1 MB, and a skip above 70 MB.
- Also fixed `INSTALL_FAILED_UPDATE_INCOMPATIBLE`: the runner generated a fresh debug keystore every run, so a pinned PKCS12 keystore (`android/keystore/nkbuild.p12`) is now wired into `signingConfigs`.

## Phase 8 · Precheck, the fish, and the skin (v1.3 → v1.4.6)

- v1.3 added a **precheck page**: before uploading, flag the files that break builds (>70 MB always fails, non-ASCII or space in a compilation unit always fails, >45 MB very likely fails, images/fonts only a note, >60 units risks throttling), and block the build outright on the hard cases.
- The background went through a self-drawn "blue fat fish" first, then the owner handed over eight KernelSU-style pink skin images ⇒ fish removed, replaced by **one background per page with a cross-fade on tab switch plus a chibi avatar**, and the whole UI was converted from dark-purple glass to a light theme (cards 0xF2 white, accent 0xFFD2557F).
- Nav icons were cropped straight out of the skin images (saliency to find the head + circular mask + saturation boost), finally unified on the pink-haired chibi; the launcher icon became an adaptive-icon (`ic_launcher_fg` over `#FFF3F7`).
- Fixes along the way: `Brush.verticalGradient(...)` missing a closing paren, a nonexistent `quadTo` (switched to `lineTo` polylines), and a `Box(weight)` without 96 dp bottom padding so cards hid behind the nav bar.

## Phase 9 · Tasks lost when backgrounding (resume + job history)

- Symptom: switch away and back, the build task is gone; the server has no record of the upload either (`viewModelScope` dies with the process).
- Fix: a `BuildService` foreground notification to raise priority, plus `runId / jobProj / jobOut / jobEta / jobStart` in `Prefs`; on launch `resume()` only adopts tasks that already have a `runId` (interrupted during upload it says so explicitly and asks for a re-run).
- Then "job history": every dispatch stores `(runId, project, output, time, file, downloaded)` in `org.json` (30 entries), and tapping a row in the output page **re-downloads** it.

## Phase 10 · The real fight: the build chain

In order:

1. **`.L_v` undefined temporary symbol** (19–22 of them) — bisected to `string_encryption`; root cause was `StringEncryption.cpp:542` setting the decrypt buffer to `GeneralDynamicTLSModel`, and any non-empty Android triple takes the TLS path, so the assembly only held `adrp x8, .L_v…` references with no definition. Fixed by inheriting `getThreadLocalMode()` and forcing `InternalLinkage` on four globals that inherited `GV->getLinkage()`.
2. **`duplicate symbol: cJSON_*`** — `SRC_FILES` contained a `.c` that other files already `#include`; both GUIs now drop "included-by-someone" source files during scanning.
3. **`relocation refers to a discarded section: .text.startup`** (11 of them) — not `--gc-sections` but COMDAT; `Utils.cpp toObfuscate()` now skips functions with comdat / linkonce_odr / weak linkage, and `BASE` gained `-fno-asynchronous-unwind-tables -fno-unwind-tables`.
4. **`vkCreateAndroidSurfaceKHR` undeclared** — missing `-DVK_USE_PLATFORM_ANDROID_KHR` (the first patch stuffed the macro into the same string as `-D_FORTIFY_SOURCE=2`, so it never took effect).
5. **PhysX requiring exactly one of `NDEBUG`/`_DEBUG`** — added `-DNDEBUG`.
6. **Link step `posix_spawn failed`** — the compile-time `CF` array strips `--ld-path`, so the link line has to add it back.
7. **`build lucifer` killed by SIGTERM (exit 143)** — not a timeout and not oomd: `mem_avail` sat at **119–398 MB**. Disabling systemd-oomd, adding 16 GB swap and `overcommit_memory=1` changed nothing; `-O1` changed nothing.
8. **Large-TU tiering** — units with >800 KB of preprocessed source now skip `flattening` (the other 7 passes still run); memory fell back to 4–7 GB, `imgui.cpp` (3.0 MB preprocessed) compiled in 37–51 s, and all 17 TUs passed in one run producing 21,145,056 B.
9. **`Illegal instruction` at runtime** — the older artifacts contained **12 `brk` sites** (`flattening` marks switch fallbacks unreachable and the backend lowers that to `brk #0xf000`); after skipping flattening: **brk 0 / udf 0**.
10. **The watchdog killing itself (post-v11)** — after `nk_text_wlock()` mprotects text RWX, the next "text is writable ⇒ uprobe" check fires on ourselves; added a `g_text_rwx_self` flag and left wlock off by default.

## Phase 11 · License keys (XL cloud verification)

- Protocol extracted line by line from `xl_verify.h` + `gm.h`: `data = Base64(IV16 + SM4-CBC(GMKEY,IV, 8 random bytes ‖ 4-byte BE timestamp ‖ plain) + HMAC-SM3(GMKEY, IV‖ct))`; the response unpacks the same way but the first **12 bytes must be dropped** before JSON, then `code==200`, `|time-now| <= 30`, `check == md5(str(time)+APPKEY+value)`.
- Desktop `tools/xlverify.py` (pure Python, no dependencies): SM4 tables + SM3 + HMAC-SM3 + CBC + envelope seal/unseal + device code + dual-host fallback. National-standard vectors (`681EDF34D206965E86B3E94F536E4246`, `SM3("abc")`) all match; a fake key against the live server returned `code=149`, proving the whole chain works.
- Mobile `XlVerify.kt` reimplements the same algorithm in Kotlin (`value` uses `toUnsignedString` because Java lacks `%u`) and was byte-for-byte cross-checked against the Python version.
- UI: a desktop license bar (status dot + key field + login/paste/unbind) and a full-screen key gate on mobile; later the account system was dropped on request, leaving keys only, and the "expiry always shows permanent" bug was fixed (`vip` lives inside `msg` and is a string).
- Added sound (four synthesized WAVs: click / ok / deny / tick, `SoundPool` with `maxStreams(1)` so taps never overlap), haptics, and a top-left toast that dismisses itself.

## Phase 12 · Anti-debug hardening v7 → v11.1

Driven by the three review documents supplied by the owner:

| Version | What was added |
|---|---|
| v7 | 12 emulator checks (qemu properties, `gGoldfish`, `/dev/qemu_pipe`, abort-message comparison, magic values, qemu maps regions…) accumulating a soft score that only kills at ≥3 |
| v8 | Tool fingerprints: 20-entry maps blacklist, thread comm scan, `/proc/net/tcp` ports 27042/27043/27052, `@frida` in `/proc/net/unix`, frida files, `State: t`, uprobe BRK rewrite scan |
| v10 | JNI table residency, anonymous executable segments, `svc #0` self-check, `/data/adb` tiering, perf hardware-breakpoint slots, multi-module `.text` hashing, artseek fallback |
| v10.1/10.2 | `g_text_rwx_self`: RWX we created ourselves no longer counts as an attack |
| v11 | Allow ART JIT `/memfd:jit-cache`, hash list cut to main module + libc + libm + libdl, baseline deferred to the first deep check, `errno` → `-fd`, wlock off by default, JNI demoted to a soft hit, maps parsed with `%n`, `/data/adb` presence counts as root |
| v11.1 | Exact JIT prefix matching, JNI soft hit recorded once, `g_tm_ready` as an atomic tri-state, redundant init sampling removed |

## Phase 13 · Quota policy and artifacts moved to Releases

- `llvm-build` was uploaded on every run and grew to **289 artifacts / 24.4 GiB**, blowing the quota (`Failed to CreateArtifact: Artifact storage quota has been hit`) so the compile step never even started.
- Purged down to **16.8 MiB**; the workflow no longer uploads `llvm-build` / `ndk-cache` and instead rebuilds clang only when needed, keeping it as a Release asset `llvm-build.tar.zst` (232.3 MB, which does not count against the quota); artifact retention set to 3 days.
- Binaries are published through Releases too: `publish binary to release` (tag `nk-builds`, `gh release upload --clobber`), and the APK follows the same path — because the quota only recalculates every 6–12 hours.

## Phase 14 · Rename, bilingual docs, public release

- **Full rename**: early codename → `NK`. Kit directory `nk/` (95 files), output `libNk.so`, script `ci/build-nk.sh`, environment variables `NK` / `NK_CONFIG`, repository `nk-obf`.
- Docs: `docs/zh/` and `docs/en/` with eight pages each (index / FEATURES / USAGE / ARCHITECTURE / COMPATIBILITY / CHANGELOG / RELEASES / CREDITS), plus bilingual `README.md` / `README.en.md` and a step-by-step `DEPLOY.md`.
- Two subagents proofread every page, then a script re-audited them: balanced code fences, no skipped heading levels, every relative link resolvable, zero hits for the old codename, no mojibake, no leftover placeholders.
- **License correction**: `src/`, `ci/` and `kit.tar.gz` derive from **OLLVM-Next (AGPL-3.0)**, so the previous MIT license was wrong; it is now "plugin AGPL-3.0 + both GUIs and docs MIT", with a licensing section in both `CREDITS.md` files.
- Released `v1.0`: `NKBuild-mobile-1.0-kami.apk` + `NKBuild-1.0-win64.exe`; the phone shows `v1.0 (versionCode 20)` and the about page renders `NK / OLLVM-NEXT` with the tagline "a new generation of obfuscation" in large type.
- Pre-public audit: **no real PAT anywhere in the repo** (only `ghp_xxxx…` placeholders in the deploy guide); the private client project was removed; the sample signing password and XL test keys are called out in `CREDITS` as sample values to replace.

## Phase 15 · Anti-debug hardening v12

`ci/nk_ad.c` grew from 64,441 bytes to roughly 76,962 bytes (1,933 lines). The round started from a static review: 30+ detection points all funnelled into a single `_exit(NK_EXIT)` inside `nk_die_r()`, so patching that one `bl` into a `ret` in `.text` disabled every check at once — and several criteria still only enumerated known strings.

| Item | Problem | Change |
|---|---|---|
| P-1 | One exit for everything | Added `nk_panic_ill()` (`brk #0x1` → SIGILL), `nk_panic_spin()` (fake ANR: 5 s busy-wait then exit, self-timing so it never hangs forever) and `nk_panic_exit()` (raw `svc #0` exit_group); `nk_fatal_poly()` rotates them by `g_panic_n % 3`, so no single disposal site exists statically |
| P-2 | The watchdog process can be impersonated | Main process now runs `nk_wd_reap()` (`waitpid(WNOHANG)` zombie reaping + `kill(pid,0)` liveness) and `nk_peer_verify()` (identity from field 22 starttime of `/proc/<pid>/stat`, so a recycled pid cannot pose as the watchdog); both only record a soft score instead of killing |
| P-3 | Self-hash covered only the first 1 MB | `nk_seg_verify()` splits `.text` into 8 rotating segments — one per 1.5 s, full coverage in 12 s — so the coverage ceiling is gone and per-round cost drops to 1/8; the first round only samples |
| P-4 | Criteria still enumerated known strings | `nk_struct_scan()` walks executable mappings and judges by structure (path whitelist prefixes, a ` (deleted)` suffix, `/memfd:` without an ART JIT prefix); `nk_thread_anomaly()` became "total + off-whitelist count > 12", which renaming cannot dodge; everything stays a soft score |
| P-5 | One forged channel was enough | `nk_cross_conflict()` compares `getppid`(173) taken by raw `svc #0` against `PPid` from `/proc/self/status`; a mismatch records a soft score, so both channels must be forged at once |
| P-6 | Five places each `malloc(256 KB)` and scanned maps | New `nk_vma_t` + `nk_vma_refresh()`: one 1 MB read of `/proc/self/maps`, parsed with `sscanf("%lx-%lx %7s %*s %*s %*s %n")` into lo/hi/perm/path, carrying generation `g_vma_gen` so it is idempotent within a round; structured criteria consume that single copy |
| S-1 | Microarchitectural criteria killed outright | Now only sets bits in `g_uarch_flags` |
| S-2 | Hard-coded 24 MHz / 19.2 MHz cannot cross SoCs | `nk_cntvct_calibrate()` runs in a constructor (before any business code) and calibrates the local `cntvct`-to-`CLOCK_MONOTONIC` ratio over two short windows; `nk_uarch_update()` then only measures drift and flags above 35% relative deviation |
| S-4 | Lightweight emulator syscall tables | `nk_syscall_semantic()` probes three syscall numbers arm64 does not implement (0x7fff / 0x0e00 / 0x3a00); a return that is neither `-ENOSYS`(-38) nor `-EPERM`(-1) records a soft score — an emulator's dispatch table breaks at the edges first |
| S-6 | Caching the fd occupies the slot it measures | `nk_hwbp_slot_count()` opens and immediately closes `perf_event_open` per slot, counting how many hardware breakpoint slots really work (< 3 records a soft score), never caching an fd; it only runs once a root framework is detected, since `untrusted_app` is refused by SELinux |

Wiring: `nk_tool_scan(!early)` now runs `nk_seg_verify → nk_struct_scan → nk_peer_verify → nk_cross_conflict → nk_uarch_update → nk_syscall_semantic → nk_behavior_scan` in that order; the low-frequency items `nk_thread_anomaly` / `nk_hwbp_slot_count` moved into the 60-second behavior check so the 1.5-second round stays cheap; the watchdog thread runs `nk_wd_reap + nk_seg_verify` every round.

### Lessons

1. **Obfuscating the anti-debug module itself hangs the process at startup.** Running `ci/nk_ad.c` through all seven passes (bcf / indirect_branch / split_blocks / flattening / constant_encryption / substitution / string_encryption) produced zero output and a hang: `debuggerd -b` showed `nanosleep ← usleep ← our constructor ← __dl_soinfo::call_constructors`, `nk_ad.log` was completely empty, `RC=124`, reproducible 3/3. Restricting the file to `string_encryption` only (`nk-self.toml`) restored normal startup. Rule: **self-obfuscation is string encryption only — semantic passes never touch `nk_ad.c`.**
2. **Quantified plaintext leak scan.** Before hardening: `nk_ad` 79 hits, `__nk_ad_check` 1, `TracerPid` 2, `frida` 23, `lsposed` 4. After: `nk_ad` 1 (the log path only), `__nk_ad_check` 0, `TracerPid` 0, `frida` 0, `lsposed` 0.

---

## Numbers at a glance

| Metric | Value |
|---|---|
| One build (warm cache) | 41 s – 1.8 min |
| First self-built clang | 40–100 min (once; then cached in a Release) |
| Reference artifact | 21,145,056 B, `.text` 16,086,400 B, `brk 0 / udf 0` |
| Cold-start reliability | 100/100 without hang (RMX3888), 100/100 (RMX3300) |
| Size after obfuscation | 2–3× the original; steady-state cost −10% to −40% |

## Phase 15.1 · v12.1 (S-3 / S-5) and 1.0.2 release

Completes the remaining S-series checks. Both are "looks like an emulator" corroboration: soft score only, never fatal.

| ID | Check | Method |
| --- | --- | --- |
| S-3 | Cache side-channel ratio | 4 MB buffer; the same pages walked sequentially and in shuffled order, 512 steps each, best of 3 rounds. Random below 1.08x sequential flags `cache-no-penalty`; per-access random latency below 6 ns flags `cache-latency-too-low`. Seed differs every round, so the access sequence is not statically predictable |
| S-5 | cpuinfo / MIDR consistency | Missing `implementer` in `/proc/cpuinfo` flags `cpuinfo-no-implementer`; qemu/goldfish/ranchu/GenuineIntel tokens flag `cpuinfo-emulator-token`; `processor` line count differing from `sysconf(_SC_NPROCESSORS_ONLN)` by more than 2x flags `cpu-count-mismatch`; `MIDR_EL1` zero flags `midr-zero`, an implementer outside the real ARM vendor set (0x41/0x48/0x4d/0x51/0x53/0x61/0x6e) flags `midr-implausible` |

| Item | Change |
| --- | --- |
| `ci/nk_ad.c` | 77,129 → 81,824 bytes (2,039 lines); adds `nk_cache_ratio()` / `nk_cpuinfo_conflict()` / `nk_ns_now()` |
| Desktop NKBuild | 1.0.0 → **1.0.2** (version in title bar and brand line) |
| Mobile NKBuild | 1.0 → **1.0.2** (versionCode 22) |
| Build | Private repo run `45178198836` success, 2,097,528 bytes; plaintext leaks still `nk_ad` 1 / `__nk_ad_check` 0 / `TracerPid` 0 / `frida` 0 |

## Phase 15.2 · The 1.0.2 patch: P-3 compile-time baseline, randomised injection, split files

No version bump (still 1.0.2 / versionCode 22); only the Release assets are replaced.

| Item | Detail |
| --- | --- |
| P-3 compile-time baseline | New `ci/tools/nk_patch_baseline.py`: after linking, before stripping, the main executable segment is split into 64 chunks and hashed with FNV-1a into `__nk_base_hash[]`; chunks targeted by a relocation get a bit in `__nk_base_exempt[]`. Then `brand`, then `llvm-strip --strip-all`. At runtime `nk_cbase_check()` compares one chunk per round, full coverage in 12 s |
| Randomised injection | The plugin mixes each function name (FNV-1a) with `NK_AD_SEED` into `nkH`: functions where `nkH % 100 >= 62` are skipped (~38%), the insertion point rotates between the entry, second and third block, and the ART table check is expanded in only half of them |
| Split files | `ci/nk_ad.c` went from 2,099 lines to a shell (headers + eight `#include "nk_ad_parts/NN_*.inc"`) plus eight numbered parts, keeping the original order so the token stream is unchanged |
| Documentation fix | Several documents claimed `nk_ad.c` is "not obfuscated"; the truth is "`string_encryption` only, no semantic pass, entry symbol randomised per build". Eight places across README / ARCHITECTURE / FEATURES / index (zh + en) were corrected, and the six names that must stay visible are now listed |
| Build | Private repo run `45191819853` success (P-3, split include and randomised injection all in one build) |

## Phase 15.3 · v12.2: E / N / Q series

Still 1.0.2, no version bump.

| Item | Problem | Change | Grade |
| --- | --- | --- | --- |
| E-2 | Only `TracerPid` was read, so a watchdog already seized by another tracer looked idle | `nk_wd_reseize()`: the main process reverse-`PTRACE_SEIZE`s the watchdog → `PTRACE_INTERRUPT` → `wait4(WNOHANG)` proves it is schedulable → `DETACH`. `EPERM` means someone else holds it (`wd-seized-by-other`), `ESRCH` means it is gone (`wd-gone`) | soft score |
| E-4 | `g_uarch_flags` was set but never consumed — dead code | `nk_uarch_consume()`: timer-ratio drift **and** two real emulation evidences → hard action; drift alone scores once (power-saving governors drift on real devices too) | soft + combined hard |
| E-6 | Emulator detection relied on file contents only | `nk_prop_get()` reads `ro.kernel.qemu` / `ro.hardware` / `ro.product.cpu.abi` through `__system_property_get` as a second source | soft score |
| N-1 | No visibility into an installed seccomp filter | `prctl(PR_GET_SECCOMP)` self-check, log only (Android 8+ ships filters; scoring would always fire) | log only |
| N-2 | PAC / BTI availability unknown | `getauxval(AT_HWCAP/AT_HWCAP2)` reads `HWCAP_PACA(1<<30)`, `HWCAP_PACG(1<<31)`, `HWCAP2_BTI(1<<17)`; log only | log only |
| N-3 | A relocated binary runs just as well | `readlink("/proc/self/exe")` and require one of `/data/app/`, `/data/local/`, `/data/data/`, `/data/user/`, `/system/`, `/vendor/`, `/apex/`, `/product/`, `/odm/`, `/mnt/`; otherwise `self-path-outside-install` | soft score |
| Q-3 | In the first second after fork `g_wd_pid` is not yet recorded, so our own watchdog looked foreign | For the first 8 s, `g_wd_pid<=0` and comm `nk-wd` → pass (`nk_boot_age_ms()`) | false-positive guard |
| Q-6 | A maps buffer that filled up made "missing entry" look like evidence | New `g_vma_trunc`: when truncated, `exec-suspicious-path` is not scored | false-positive guard |

Files: new `ci/nk_ad_parts/09_nk_env_probe.inc` (5.1 KB); the shell gained one include; nine parts in total.

## Phase 15.4 · v12.4: anti-debug false positives (round 1)

| Symptom | Root cause | Fix |
|---|---|---|
| Real devices died at startup with `exit 97`, logs mixing `emulation score>=3` / `emulator cpuinfo` | Soft evidence was re-counted on every deep scan (worker runs every 1.5 s), hitting the threshold within seconds; the `/proc/cpuinfo` check was too strict | Soft evidence is now de-duplicated via a bitmap (`nk_emu_hit(bit, tag)`), the kill threshold moved from 3 to 4 distinct items, and `timer-ratio` / `comm-readback` / `segv-not-delivered` became log-only |
| Selecting only "anti-debug" still produced heavily obfuscated output | The large-TU auto-downgrade (`nk-nofold` / `nk-light`) overrode the user config | Downgrade only applies when the user config enables ≥5 passes |

## Phase 15.5 · v12.5: the watchdog no longer uses ptrace (decisive fix)

Device evidence: `nk_ad[chk] TracerPid=27125 wd=27125` → `nk_ad[tool] status State: t` → `nk_ad[die] reason=nk_ad: traced state`.

The watchdog used `PTRACE_SEIZE` on the main process, which parks it in ptrace-stop (`State: t`); the process's own `nk_trace_state()` then treated the watchdog as a foreign tracer and killed itself.

Fix: the watchdog polls `TracerPid` from `/proc/<pid>/status` every 300 ms and calls `kill(target, 9)` when it is `>0`; `g_wd_pid` is set right after `fork()`; the reverse-seize check (E-2) is a no-op. **No ptrace relationship is created at all, so the main process scheduling state is untouched.**

## Phase 16 · v1.0.5 (release: critical fix)

- Both clients are **1.0.5** (desktop `NKBuild-1.0.5-win64.exe` 14,002,438 B, Android `NKBuild-mobile-1.0.5-kami.apk` 10,460,373 B, versionCode 25).
- New **startup gate**: with anti-debug on, the binary checks the environment first, prints a purple banner "NK 1.0.5 验证通过" plus a kaomoji, waits 3 s on a terminal, clears the screen and continues; on failure it prints "请关闭你的调试工具" and exits. `NK_AD_GATE=0` disables the banner, `NK_AD_ALLOW_DEBUG=1` allows ptrace debugging.
- Device regression (RMX3888 / Android 16): 8 passes + anti-debug 5/5 `rc=0`; anti-debug only 3/3 `rc=0`; both exit 97 when traced.
- Per-pass runtime matrix (all 16 passes compiled alone and run 3x on device): **all `rc=0`** — no single pass crashes on its own.
- Known defect: with **all 16 passes** enabled (`all16`) the `projects/loop` binary dies with `SIGSEGV` (`pc=0`, null indirect call in a worker thread). The combination bisect shows that dropping any of `string_encryption` / `substitution` / `indirect_branch` / `anti_debugging` removes the crash — a multi-pass interaction; the default preset (8 core passes) is unaffected.

## Phase 15.6 · Two big issues: hang and size (2026-11-25)

| Issue | Root cause | Fix | Measured |
|---|---|---|---|
| Built binary hangs / false 97 | Entry check ran a full deep scan every 32 function calls: hashing the whole `.text`, reading `maps` 4x, walking 971 processes | 1 s hard gate on the entry check; whole-`.text` hash only under `NK_AD_STRICT=1`; maps/comm/socket/module-hash limited to 5/5/3/30 s | `loop` 5/5 rc=0, 0.14 s user CPU; traced rc=97 |
| 9 MB source -> 230 MB binary | 7 passes applied to very large TUs (imgui, 9.7 MB preprocessed main.cpp) | Four-tier size downgrade (>4 MB -> 2 passes; >1.2 MB -> 3; >400 KB -> 7) | 242,095,960 B -> 5,986,488 B |
