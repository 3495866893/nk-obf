# NK — Architecture

This document describes how NK is built: the compile-time plugin and the runtime watchdog as two independent halves, the fixed pass order over IR, the three-tier degradation for oversized translation units, the CI cache and Release-based distribution, and the data model behind resume and task history.

## Two halves

```
source (.c/.cpp)
   │  clang 21 frontend
   ▼
LLVM IR
   │  ┌──────────────────────────── libNk.so ────────────────────────────┐
   │  │ PassPlugin entry → PassBuilder callback → 16 module passes, fixed │
   │  │ order, each gated by its own TOML `enabled`                      │
   │  └──────────────────────────────────────────────────────────────────┘
   ▼
rewritten IR (ciphertext constants, flattened state machines, call __nk_ad_check())
   │  LLVM 21 backend
   ▼
object files (.o) ──link──► .so / executable ◄── nk_ad_extra.o (separate, not passed through the full pass set)
```

The dividing line is deliberate: **the obfuscator never writes anti-debug logic into IR.** IR receives one external call per function entry; the implementation is a plain C file compiled and linked on its own. The consequence is that a broken obfuscation pass can never break the watchdog's own control flow, and adding a detection check does not require regenerating IR.

| Half | Artifact | Runs at | Responsibility |
|---|---|---|---|
| Compile-time | `libNk.so` (target `Nk`) | Build only | Make the binary expensive to read |
| Runtime | `nk_ad_extra.o` from `ci/nk_ad.c` | Every process start | Make the environment expensive to trust |

## Plugin lifecycle

| Stage | Entry | Behavior |
|---|---|---|
| Load | `llvmGetPassPluginInfo` | Registers optimization-pipeline callbacks on the `PassBuilder` |
| Config parse | `[global]` / `[passes.*]` | Reads the TOML named by `NK_CONFIG`; a `preset` fills defaults first |
| Master switch | `NK=1` | Without it the callback returns immediately and no IR is touched |
| Module pass | `runOnModule` | Runs enabled passes in fixed order |
| Verify | `verifyModule` | Runs once after rewriting; a failure discards the change instead of emitting broken IR |

Configuration precedence: TOML named by `NK_CONFIG` > built-in `preset` > compile-time defaults. The environment is read once per process, so all units compiled by one clang invocation share one configuration.

## Pass order over IR

Order is fixed; a configuration can decide *whether* a pass runs, never *where*. Three rules shape the sequence: data encryption must precede control-flow rewriting (otherwise the decryptor itself gets flattened and runtime cost explodes), control-flow rewriting goes light-to-heavy, and injection goes last so every real function entry has a checkpoint.

| # | Pass | Stage | Why here |
|---|---|---|---|
| 1 | `string_encryption` | data | Collect strings while the IR still holds them as constants |
| 2 | `constant_encryption` | data | Establishes the constant pool later passes reuse |
| 3 | `substitution` | instruction | Per-instruction rewrite; does not disturb block structure |
| 4 | `mba` | instruction | Experimental, kept before `bcf` so size impact stays measurable |
| 5 | `vector_obfuscation` | instruction | Needs vector shapes intact |
| 6 | `bcf` | control flow | Adds the fake branches first |
| 7 | `split_blocks` | control flow | Splits into segments; those segments are what flattening consumes |
| 8 | `chaos_state_machine` | control flow | Attempted first; falls back when the block count is out of range |
| 9 | `flattening` | control flow | The heavy state machine — and the pass that big TUs skip |
| 10 | `indirect_branch` | control flow | Converts remaining direct branches after flattening |
| 11 | `function_wrapper` | structure | Function boundaries are final by now |
| 12 | `function_call_obfuscate` | structure | Call-site rewrite, last of the structural group |
| 13 | `anti_debugging` | injection | `call __nk_ad_check()` at each entry |
| 14 | `anti_hooking` | injection | Integrity baseline; disabled |
| 15 | `anti_class_dump` | injection | ObjC / Java metadata; disabled |
| 16 | `objc` | front end | ObjC only; disabled |

### What each transform looks like in IR

| Transform | IR-level change | Runtime shape |
|---|---|---|
| String encryption | `ConstantDataArray` → ciphertext bytes + decrypt call | Decrypt on first use, `memset` afterwards |
| Constant encryption | Constant hoisted to a global, split into XOR shares | Recombine once, keep the result on the stack |
| Flattening | Function body pushed into a `switch` state machine | One indirect branch per block |
| Bogus control flow | Opaque predicate + never-taken block | Predicate is always true; the fake block only occupies space |
| Indirect branch | Jump table in an `alloca`, targets XOR-encrypted | Decrypt, then branch through a register |
| Function wrapping | Original renamed, N proxy layers generated | One real call per layer |
| Anti-debugging | `call __nk_ad_check()` | Watchdog runs, returns, function continues |

### Two structural fixes that constrain the implementation

| Issue | Root cause | Current handling |
|---|---|---|
| `Undefined temporary symbol .L_v*` | String-encryption globals inherited a thread-local model; `.L_v` temporaries were dropped at link time while relocations still referenced them | The global's own thread-local mode is inherited instead of forced; four globals that copied the parent's linkage now use `InternalLinkage` |
| `relocation refers to a discarded section: .text.startup` | COMDAT / `linkonce_odr` functions were split into fresh sections; the original was discarded but relocations still pointed at it | `Utils.cpp toObfuscate()` returns `false` for `hasComdat()` / `hasLinkOnceODRLinkage()` / `hasWeakODRLinkage()` / `hasLinkOnceAnyLinkage()` / `hasWeakAnyLinkage()` |

The COMDAT skip costs strength on template and inline functions; the alternative is an unlinkable object, so the trade is accepted.

## Big-TU degradation: three tiers

Compiling large translation units under `flattening` is what produced every memory incident in the project's history: 17-file projects killed at build second 161–182 with SIGTERM(143), `mem_avail` down to 119–398 MB on a 16 GB machine whose swap was already full, `imgui.cpp` (960 KB) reproducing it alone.

The size signal is **preprocessed bytes**, not source bytes:

```bash
clang++ -E -P unit.cpp | wc -c
```

Source size lies. `main.cpp` is 1.3 KB on disk but `#include`s `pz.h` and `xz.h`, giving 3.0 MB after preprocessing — a big TU disguised as a small file.

| Tier | When | What runs | Rationale |
|---|---|---|---|
| 1. nofold | First attempt for any big TU | `ci/nk-nofold.toml`: 7 passes (`string_encryption`, `constant_encryption`, `substitution`, `bcf`, `split_blocks`, `indirect_branch`, `anti_debugging`), `flattening` explicitly `enabled = false` | Keeps most of the strength, removes the state-machine blow-up |
| 2. light | nofold still too heavy or fails to link | The light tier (`ci/ui-jni-light.toml`, published as preset `nk-light.toml`): 3 passes — `string_encryption`, `substitution`, `anti_debugging` — with lower `probability` and `iterations` | Shrinks IR before the backend sees it |
| 3. `-O1` | Still failing | Per-unit `-O1` instead of `-O2` | Backend memory drops steeply; output stays usable |

Compilation is per file (`-c`), which is what makes this possible at all: any single unit can be located, downgraded and retried without rebuilding the project.

The same tiering removes the trap problem. `flattening` marks a switch default `unreachable`; `bcf`'s opaque predicates and `split_blocks`' new blocks then make that "unreachable" branch genuinely reachable, and LLVM has already encoded the trap as `brk #0xf000`. One 17-file build carried 12 `brk` plus 8 `udf` sites and died with SIGILL on start. Skipping `flattening` and adding `-fno-asynchronous-unwind-tables -fno-unwind-tables` to the base flags took it to `brk 0 / udf 0`.

| Guard | Where | Effect |
|---|---|---|
| Preprocessed size probe | Driver script | Selects the tier before compiling |
| Per-unit `-c` | Driver script | Isolates a failing unit |
| `nk-nofold.toml` | `ci/` | 7 passes, no flattening |
| `-O1` per unit | Driver script | Final memory fallback |
| Base flags | Driver script | No async unwind tables, no explicit unwind tables |
| `--ld-path=$LLD` | Link step | Avoids the host linker |
| `-DVK_USE_PLATFORM_ANDROID_KHR` | Compile flags | Vulkan surface support |
| `-DNDEBUG` | Compile flags | PhysX `PxPreprocessor.h:454` |

## The self-obfuscation boundary of `nk_ad.c`

`ci/nk_ad.c` is compiled on its own into `nk_ad_extra.o`, and **it is not "not passed through the full pass set" — it goes through exactly one pass, `string_encryption`** (`ci/nk-self.toml`). The boundary:

1. **Strings must be encrypted.** Before hardening the shipped binary carried `nk_ad` 79 times, `TracerPid` 2, `frida` 23 as plaintext; one `strings` run listed the whole detection set. With `string_encryption` alone those counts are 1 / 0 / 0, and the single remaining `nk_ad` hit is the `/data/local/tmp/nk_ad.log` path.
2. **No semantic pass, ever.** `bcf` / `indirect_branch` / `split_blocks` / `flattening` / `constant_encryption` / `substitution` together make the artifact spin inside a **global constructor** (`debuggerd`: `nanosleep ← usleep ← call_constructors`, `State: S`, `wchan=__arm64_sys_nanosleep`, zero log lines). Compiling `nk_ad.c` as part of `SRCS` also produces `Undefined temporary symbol .L_vXXXX`.
3. **The entry symbol is re-randomised per build.** `build-lucifer.sh` exports `NK_AD_SYM="__nk_$(head -c8 /dev/urandom | od -An -tx1 | tr -d ' \n')_g"`; the plugin reads it with `getenv("NK_AD_SYM")` and calls `getOrInsertFunction`, the runtime keeps `#ifndef NK_AD_SYM / #define NK_AD_SYM __nk_ad_check` as the default. No fixed name survives in the binary.
4. **`NK_SKIP_ADB=1` stops self-reference.** Without it the `anti_debugging` pass would inject `call <NK_AD_SYM>()` into every function of the watchdog itself — recursive and pointless; `runOnFunction` returns immediately when the variable is set.
5. **Six names must stay visible.** The inlined ART table check links against `__nk_art_lo`, `__nk_art_hi`, `__nk_vm`, `__nk_ad_panic`; the P-3 baseline writer needs `__nk_base_hash` and `__nk_base_exempt`. Everything else is randomised or encrypted.
6. **It can still be switched off wholesale.** With `NK_AD=0` the file is built with `-DNK_AD_OFF` into empty implementations of `nk_die_r()` and `NK_AD_SYM()`; the rest is kept so the injected call sites still resolve.

### v12 hardening (P/S series)

The v12 rule: **anything that can stay a soft score stays a soft score, and nothing that kills keeps a single exit.** The six P items reshape how a verdict is reached and delivered; the S items target simulated execution.

| # | Problem | Change | Grade |
|---|---|---|---|
| P-1 | 30+ detection points all funnel into one `_exit(NK_EXIT)` inside `nk_die_r()`; patching one `bl` to `ret` disables the lot | `nk_panic_ill()` (`brk #0x1` → SIGILL) / `nk_panic_spin()` (fake ANR: 5 s busy-wait then exit, self-timing) / `nk_panic_exit()` (raw `svc #0` exit_group), rotated by `nk_fatal_poly()` on `g_panic_n % 3` | mechanism (no single disposal site) |
| P-2 | A recycled watchdog pid can impersonate the peer | `nk_wd_reap()` reaps zombies and probes with `kill(pid,0)`; `nk_peer_verify()` checks identity through field 22 starttime of `/proc/<pid>/stat` | soft score (avoid killing hardened shells and vendor perf daemons) |
| P-3 | Self-hash covered only the first 1 MB of `.text` | `nk_seg_verify()` rotates 8 segments, one per 1.5 s, full coverage in 12 s, per-round cost 1/8; first round samples only | hard evidence |
| P-4 | Criteria still enumerated known strings | `nk_struct_scan()` judges executable mappings structurally; `nk_thread_anomaly()` became "total + off-whitelist count > 12" | soft score |
| P-5 | Forging the `/proc` channel alone was enough | `nk_cross_conflict()` compares `getppid`(173) from raw `svc #0` against `PPid` from `/proc/self/status` | soft score |
| P-6 | Five sites each `malloc(256 KB)` and scanned maps | `nk_vma_t` + `nk_vma_refresh()`: one 1 MB read, `sscanf("%lx-%lx %7s %*s %*s %*s %n")`, `g_vma_gen` keeps it idempotent within a round | mechanism (single data source) |
| S-1 | Microarchitectural criteria killed outright | Only sets bits in `g_uarch_flags` | soft score |
| S-2 | Hard-coded 24 MHz / 19.2 MHz cannot cross SoCs | A constructor calls `nk_cntvct_calibrate()`, which pins the `cntvct` / `CLOCK_MONOTONIC` ratio over two short windows; `nk_uarch_update()` then measures drift only and flags above 35% relative deviation | soft score |
| S-4 | Emulator syscall dispatch tables fake `ENOSYS` | `nk_syscall_semantic()` probes 0x7fff / 0x0e00 / 0x3a00; a return that is neither `-ENOSYS`(-38) nor `-EPERM`(-1) scores | soft score |
| S-6 | A cached fd occupies the slot being measured | Opens and immediately closes `perf_event_open` per slot, counting usable slots (< 3 scores), never caching an fd; runs only once a root framework is detected | soft score |

Two constants drive P-4. Legal path prefixes are `/system/`, `/system_ext/`, `/vendor/`, `/apex/`, `/product/`, `/odm/`, `/data/app/`, `/data/data/`, `/data/user/`, `/data/dalvik-cache/`, `/data/local/`, `/mnt/` and `[`; ART JIT `/memfd:` prefixes are `jit-cache`, `jit-zygote`, `jit_zygote` and `jit_cache`, and a match is passed through.

Wiring: `nk_tool_scan(!early)` runs `nk_seg_verify → nk_struct_scan → nk_peer_verify → nk_cross_conflict → nk_uarch_update → nk_syscall_semantic → nk_behavior_scan`; the low-frequency `nk_thread_anomaly` / `nk_hwbp_slot_count` live in the 60-second behavior check; the watchdog thread runs `nk_wd_reap + nk_seg_verify` each round.

Two more layers landed after v12:

- **P-3 compile-time baseline.** The link step no longer strips immediately: `ci/tools/nk_patch_baseline.py` splits the main executable segment into 64 chunks, hashes each with FNV-1a into `__nk_base_hash[]`, marks every chunk that a relocation targets in `__nk_base_exempt[]`, and only then do `brand` and `llvm-strip --strip-all` run. At runtime `nk_cbase_check()` compares one chunk per round, so full coverage takes 12 s and any edit hits `nk_ad: text chunk differs from build baseline`.
- **Randomised injection layout.** The plugin mixes the function name (FNV-1a) with `NK_AD_SEED`: functions where `nkH % 100 >= 62` are skipped (~38%), the insertion point rotates between the entry, the second and the third block, and the ART table check is expanded in only half of them — no uniform injection pattern remains.

The file itself went from 2,099 lines in one unit to a shell plus eight `.inc` parts: `ci/nk_ad.c` now holds the headers and `#include "nk_ad_parts/NN_*.inc"`, numbered in original order so the token stream is unchanged.

## CI: cache layout and Release-based distribution

| Layer | Mechanism | Size / key |
|---|---|---|
| LLVM prebuilt | Release `llvm-build-21.1.8`, asset `llvm-build.tar.zst` | 232 MB, extracted to `/mnt/work` |
| Plugin | Cache keyed by `sha256(kit.tar.gz)[:12]` | `kit.tar.gz` is 527 KB in the repo |
| NDK | Cached NDK r29 tree | `/mnt/work/android-ndk-r29` |
| Rebuild fallback | `rebuild_clang=true` builds LLVM from source | Only when the Release asset cannot be restored; off by default |

Two step-level changes produced most of the speedup:

| Step | Before | After |
|---|---|---|
| `disk & deps` | Fixed `apt-get install` of the full list — 7.5 min | Install only what is missing via `command -v`, usually nothing — 0.02 min |
| `smoke test` | 4 min every run | `skip_smoke=true` by default — 0 min |

End to end: **17.6 min → 1.8 min**.

Distribution moved off workflow artifacts after the quota incident: 289 artifacts / 24.4 GiB blew through the 500 MB budget and every upload began failing with `Failed to CreateArtifact: Artifact storage quota has been hit`. After cleanup the store held 5 artifacts / 16.8 MiB, and the rule became:

| Rule | Value |
|---|---|
| Toolchains (`llvm-build`, `ndk-r29-cache`) | Never uploaded as artifacts |
| Build outputs | Published to Release `nk-builds` (`lucifer` + `nk-toolchain.tar.gz`) |
| Workflow artifact retention | `retention-days: 3` |
| Reclaiming space | `cleanup.yml` |

`systemd-oomd` is stopped, disabled and masked in the `disk & deps` step, because on Ubuntu 22.04+ it SIGTERMs the whole session under memory pressure — indistinguishable at first from a workflow timeout. A 16 GB swapfile is created for the peak of `flattening` / `indirect_branch`.

## Resume and task-history data model

The Android client is the only place where a build can outlive the UI, so the model lives there.

| Field | Type | Lives in | Meaning |
|---|---|---|---|
| `token` | string | `SharedPreferences` (`Prefs.token`) | GitHub credential |
| `kami` | string | `SharedPreferences` (`Prefs.kami`) | License key, cleared on unbind |
| `runId` | long | `Prefs` / `BuildViewModel` state | The Actions run being followed; the resume anchor |
| History entry | `(project, artifact, runId, timestamp, downloaded)` | Persisted list | One row per build, tappable to re-download |
| `BuildService` | foreground service | Android service manager | Keeps polling alive when the app leaves the foreground |

The state machine is short by design: `idle → uploading → dispatched(runId) → polling → downloading → done`, with `failed(reason)` as the only other terminal state. Because `runId` is persisted before polling starts, a process death at any point after dispatch resumes into `polling` rather than re-uploading — the expensive half is never repeated. Task history exists for the case where the run finished while the app was dead: the row still points at the artifact, and a tap downloads it without a new build.

On the desktop side there is no background service — closing the window ends the follow-up — but the same `runId` is printed in the log pane so the run can be reopened in a browser.
