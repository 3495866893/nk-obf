# Changelog · From the First Line to v1.0

Chronological record of NK, from "the plugin cannot link" to "two GUIs plus a public release". Each entry states **symptom → root cause → fix → verification numbers**.

Naming: the plugin was originally `Ensia` (repo `ensia-plugin`); it was renamed wholesale on 2026-11-23 to `NK` (repo `nk-obf`), `libEnsia.so → libNk.so`.

---

## Stage A · Making the plugin produce linkable output (2026-11-21)

### A1 `Undefined temporary symbol .L_v…`

- **Symptom**: every project with `string_encryption` enabled failed with dozens of `error: Undefined temporary symbol .L_v<16 hex digits>`, then `fatal error: too many errors emitted`.
- **Isolation**: an all-passes-off config (`ci/plain.toml`) compiled fine, so a pass was responsible. Bisecting the 8 core passes: `only-str` failed, `only-const` / `only-sub` / `only-bcf` passed → `string_encryption` alone.
- **Root cause**: `StringEncryption.cpp:542-544`
  ```cpp
  GlobalValue::ThreadLocalMode tlsMode = M->getTargetTriple().empty()
      ? GlobalValue::NotThreadLocal : GlobalValue::GeneralDynamicTLSModel;
  ```
  The Android triple is never empty, so the decrypt buffer became a thread-local global. The assembly then contained `adrp x8, .L_v…` references with no definition, and the `.L` prefix marks a temporary symbol in ELF, which the MC layer rejects.
- **Fix**: `tlsMode = GV->getThreadLocalMode();` plus forcing `GlobalValue::InternalLinkage` on the four globals that had inherited `GV->getLinkage()`.
- **Verification**: `ci/only-str.toml` compiles with **0 errors**; the real 8-pass config produced a 19,595,824-byte binary.

### A2 `relocation refers to a discarded section: .text.startup`

- **Symptom**: compile succeeded, link failed with 11 discarded-section relocations.
- **Investigation**: `--no-gc-sections` changed nothing (so it was not `--gc-sections`). Making `FeatureElimination` skip the "linkonce/weak → internal + clear COMDAT" rewrite made things worse (`~vector`, `basic_string`, `__clang_call_terminate` all discarded), proving that internalisation step is required.
- **Root cause**: the pass split COMDAT functions, but COMDAT semantics are all-or-nothing per group; splitting one member means the linker drops the group and every reference dangles.
- **Fix**: hard skip in `Utils.cpp::toObfuscate()`:
  ```cpp
  if (f->hasComdat() || f->hasLinkOnceODRLinkage() || f->hasWeakODRLinkage()
      || f->hasLinkOnceAnyLinkage() || f->hasWeakAnyLinkage()) return false;
  ```
  plus `-fno-asynchronous-unwind-tables -fno-unwind-tables` in `BASE` (CFI symbols referencing discarded sections are the second entry point for the same failure).
- **Verification**: all 17 translation units `rc=0`, link `rc=0`, 85,642,896-byte output.

### A3 The anti-debug code killed itself

- **Symptom**: builds with `anti_debugging` **exited silently at startup** — no output, no crash log.
- **Isolation**: a probe proved the first `PTRACE_TRACEME` returns 0 and the second returns `-1 EPERM`; the pass injected one TRACEME per function, so the second call always failed and was read as "being debugged" → violent exit.
- **Fix**: `AntiDebugging.cpp` no longer injects `PTRACE_TRACEME`; it injects `call __nk_ad_check()` at each function entry and delegates the decision to the runtime module `ci/nk_ad.c`, which forks its own watchdog and only counts foreign tracers.
- **Verification**: `NK_AD_DEBUG=1` logs `TracerPid=0 wd=0 hooked=0`; normal start prints the app UI; attached by `nk_spawn` → `exit 97`.

---

## Stage B · Runtime watchdog `nk_ad.c` (v1 → v11.1)

It is deliberately **excluded from obfuscation** (compiled separately with `clang -x c` into `nk_ad_extra.o` and linked), because running the detector through the passes produced `Undefined temporary symbol` errors and shredded its logic.

| Version | Added | Why |
|---|---|---|
| v1–v4 | `TracerPid` probe, all-thread scan (`/proc/self/task`), maps diff, libc prologue scan, self `.text` FNV, `prctl(PR_SET_DUMPABLE,0)`, raw `svc #0` syscalls (openat=56 / read=63 / close=57 / write=64 / getdents64=61), fail-open, `_exit(97)` on hit | TracerPid alone was defeated by Frida plus ptrace |
| v4 | Watchdog process whitelisted via `/proc/<pid>/comm == "nk-wd"`; maps diff narrowed from "any new r-x" to "rwx only" | Vulkan/GL drivers legitimately `dlopen` at runtime |
| v8 | `nk_maps_hit` (20 signatures), `nk_comm_hit` (per-tid comm), `nk_frida_socket` (LISTEN ports 27042/27043/27052, `@frida` in `/proc/net/unix`), `nk_frida_files`, `nk_trace_state`, `nk_uprobe_brk` (scan first 8192 B of `.text`), `nk_hwbp_hiccup` | User supplied a 204-line list of debugging tools |
| v10 | `nk_jni_table_ok`, `nk_anon_exec`, `nk_svc_ok`, `nk_root_framework`, `nk_hwbp_slots` (perf + `HW_BREAKPOINT_X`), `nk_tm_collect/verify` (multi-module `.text` hashes) | External review `mj (3).md`, 8 recommendations |
| v10.2 | `g_text_rwx_self`, `NK_AD_NO_WLOCK` | After `nk_text_wlock()` made the text RWX, the next "text is writable" check flagged the process itself |
| v11 | memfd allows ART JIT (`/memfd:jit-cache`), hash list cut to main module + libc + libm + libdl, baseline deferred to the first deep check, `errno → -fd`, `g_wlock_tried` off by default, JNI demoted to a soft score, maps parsed with `%n`, `/data/adb` presence counts as root | `mj (4).md` reported two "will crash on device" P0s: ART JIT memfd killed as injection, and ART patching its own `.text` |
| v11.1 | `jit` prefix matching (blocks `/memfd:frida-jit-agent`), `g_jni_soft_done` records once, `g_tm_ready` atomic three-state, removed redundant init sampling | `mj (5).md`: substring `strstr(q,"jit")` was bypassable, soft scores accumulated to 3 and killed the process 4–6 s after launch, `g_tm_ready` was non-atomic |

**Decision semantics**: only a foreign tracer or a signature hit leads to `_exit(97)`; unreadable files fail open; `NK_AD_DEBUG=1` writes each step to `/data/local/tmp/nk_ad.log`.

---

## Stage C · CI from 17.6 minutes to 1.8 minutes

- `disk & deps` became conditional instead of a fixed `apt-get`: **7.5 min → 0.02 min**.
- `smoke test` defaults to skipped (`skip_smoke=true`): **4 min → 0**.
- LLVM prebuilt artifacts moved to Release `llvm-build-21.1.8` (`llvm-build.tar.zst`, 232 MB); the plugin cache is keyed by `sha256(kit.tar.gz)[:12]`; NDK cache hits.
- **End to end: 17.6 → 1.8 minutes.**

### C1 Quota exhaustion

- **Symptom**: a run failed after 19 s with only `##[error]Failed to CreateArtifact: Artifact storage quota has been hit.`; `build lucifer` never ran.
- **Root cause**: 500 MB private-repo quota versus **289 artifacts / 24.4 GiB** (about 90 copies of `llvm-build` at 231 MB, plus 786 MB of NDK cache).
- **Fix**: purged to **5 artifacts / 16.8 MiB**; the workflow stopped uploading `llvm-build` / `ndk-r29-cache` as artifacts; binaries publish to Release `nk-builds`; artifacts use `retention-days: 3`.
- **Side lesson**: re-adding a `retention-days` key inside an existing block creates a duplicate YAML key and GitHub dispatch answers **422 Unprocessable Entity**.

---

## Stage D · Three failures on a large project (2026-11-22)

### D1 `build lucifer` killed by SIGTERM (exit 143)

- **Symptom**: the 17-file project was killed 161–182 s into `build lucifer`; the log contained only `##[error]Process completed with exit code 143.` and `The runner has received a shutdown signal` — not a single compiler error.
- **Ruled out**: `timeout-minutes` (350), `concurrency`, OOM exit 137, `systemd-oomd` (disabling it and adding 16 GB swap changed nothing).
- **Root cause**: only visible after changing the resource monitor from `> /tmp/nk_res.log` to `| tee -a` — **`mem_avail` sat at 119–398 MB** (16 GB host, swap full). The job was terminated for memory exhaustion.
- **Fix, three layers**: (1) compile file by file with `-c` instead of handing 17 files to one clang invocation; (2) large TUs try `ci/nk-nofold.toml` (7 passes, no `flattening`) and only then fall back to the light tier (`ci/ui-jni-light.toml`: `string_encryption`, `substitution`, `anti_debugging`); (3) large TUs compile at `-O1`.
- **Detection metric**: preprocessed size, not source size — `projects/jni/main.cpp` is 1.3 KB of source but 3,123,227 B after `-E -P | wc -c` because of `#include "pz.h"` / `"xz.h"` / `"Encrypt.h"`.

### D2 `Illegal instruction` (SIGILL)

- **Symptom**: obfuscated binaries died with SIGILL immediately.
- **Localization**: scanning `PT_LOAD|PF_X` segments showed **12 `brk` + 8 `udf`** in a 35,519,648-byte code section, while older builds had none. Disassembly showed `ldr w8,[x23,#0x28f4]; cmp w8,w20; b.ne +4; brk #0xf000` — a flattened dispatcher state check.
- **Root cause**: `flattening` marks the switch default `unreachable`, and `bcf` opaque predicates plus `split_blocks` blocks make that "unreachable" path actually reachable; LLVM lowers `llvm.trap`/`unreachable` to `brk #0xf000` on AArch64.
- **Fix**: large TUs skip `flattening`; `-fno-asynchronous-unwind-tables -fno-unwind-tables` added to `BASE`.
- **Verification**: new binary has a 16,086,400-byte code section with **`brk 0 / udf 0`**.

### D3 Compile/link errors fixed along the way

| Error | Fix |
|---|---|
| `use of undeclared identifier 'vkCreateAndroidSurfaceKHR'` | Add `-DVK_USE_PLATFORM_ANDROID_KHR` as its own array element (embedding it inside the `-D_FORTIFY_SOURCE=2` string corrupts that macro) |
| `PxPreprocessor.h:454: Exactly one of NDEBUG and _DEBUG needs to be defined!` | Add `-DNDEBUG` |
| `clang++: error: unable to execute command: posix_spawn failed` | The compile flags array filters out `--ld-path`; add `"--ld-path=$LLD"` to the link line |
| `ld.lld: error: duplicate symbol: cJSON_*` | Do not submit sources that are `#include`d by another source |
| `[3/3] compile …/nk_ad_extra.o` treated as a source | `case "$f" in *.o\|*.a) objs+=("$f"); continue ;; esac` |

---

## Stage E · Desktop GUI

- Rewritten as `tools/NKBuild.py` (CustomTkinter, ~1000 lines): four cards (source / project·output / anti-debug / start) plus log pane, progress, ETA, abort, open-output.
- **Key fixes**
  - Scanning produces two lists — compilable units and upload files. Uploading only `.cpp` files makes CI fail with `imgui.h file not found`.
  - Blob upload retries with backoff (1 / 2 / 5 / 20 MB all return 201; the blob endpoint allows 100 MB. The 1 MB limit applies to `PUT /contents`, used only for `ci/ui-<proj>.toml`).
  - Sources that are `#include`d elsewhere are dropped to avoid duplicate symbols.
  - Output directory falls back through seven candidates (exe dir → Desktop → Documents → `~` → LOCALAPPDATA → LocalLow → TEMP) after a `PermissionError [WinError 5]` under a restricted token.
  - Failures now list the failing step names and the run URL instead of `!! build failed: failure`.
- **1.6.0 added the license bar**: `tools/xlverify.py` implements the SM4-CBC + HMAC-SM3 envelope, device code, login/unbind; `start()` refuses to run unlicensed.

---

## Stage F · Android GUI (v1.1 → v1.8.1)

| Version | Change |
|---|---|
| 1.1 | First usable build: pick a directory, build, download |
| 1.2 | Per-pass selection (16 entries) with locally generated TOML; artifacts auto-saved to `Download/NKBuild` with one-tap install; ETA; liquid-glass bottom nav (Haze) |
| 1.3 | New "precheck" page: flags dangerous files before upload (>70 MB fatal / >45 MB likely fatal / spaces or non-ASCII in a compilation unit fatal / images, fonts, documents just waste time / >60 units risks rate limits) |
| 1.4 | Switched to five open-source skin backgrounds (one per page, 420 ms `Crossfade`), chibi girl as app icon, nav icons and header avatar, light theme (`#F6EFF4` / `#D2557F`); 1.4.2–1.4.6 tuned contrast, card opacity, log pane and icon framing |
| 1.4.1 | Background-safe builds: foreground service `BuildService` plus `runId` resume |
| 1.5.x | Included sources skipped automatically; "task history" card (`project·output + run id + time + downloaded?`, tap to re-download) |
| 1.7 | Account registration/login, key recharge, announcements, cloud version check (`api=ini` `version`) |
| 1.8.0 | Four button sounds (`SoundPool`, single stream, 55 ms throttle), graded haptics, auto-dismissing toast in the top-left corner |
| 1.8.1 | Key-only flow (accounts removed per request); fixed "unactivated account never reaches the recharge screen" (server returns `code=179`); fixed "key already used" (keys bind once — unbind first); fixed expiry (server returns `msg.vip` as a string of seconds, e.g. `4102243200`) |

- **Pinned signing key**: `android/keystore/nkbuild.p12` (PKCS12, self-signed, 10,950 days) — previously every CI run generated a fresh debug keystore, causing `INSTALL_FAILED_UPDATE_INCOMPATIBLE`.
- **Versioning**: `versionName` was pinned at `1.8.1` in this stage, only `versionCode` incrementing; the published line now reads `1.0`.

---

## Stage G · Rename and release (2026-11-23)

- Renamed `Ensia → NK` everywhere: kit directory `ensia/ → nk/` (95 files), `libEnsia → libNk`, `ENSIA → NK`, `build-ensia.sh → build-nk.sh`, repo `ensia-plugin → nk-obf` (still private).
- Sources flattened into `src/` (79 files / 2.5 MB), 7 documents, MIT license.
- Repo cleanup: removed `proj/`, `projects/myproj/` (37.4 MiB, `font.h` alone is 20 MB) and `projects/路西法/`; **410 files / 63.3 MiB → 294 files / 24.9 MiB**.
- Published **Release `v1.0`**: `NKBuild-mobile-1.0-kami.apk` (9.98 MB) and `NKBuild-1.0-win64.exe` (13.35 MB); plus `nk-builds` (every build's `lucifer` and `nk-toolchain.tar.gz`) and `llvm-build-21.1.8`.
- Latest CI: run `45041678195` success, `build lucifer` **41 s**, output 21,145,056 B, sha256 `6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34`, code section 16,086,400 B, `brk 0 / udf 0`, zero plaintext occurrences of `yz.dujiuyue.cn` and `kami`.
