# NK — Feature Reference

This document lists what NK actually does: the 16 LLVM passes with their keys, effects and defaults, the full runtime detection set inside `ci/nk_ad.c` grouped by what it defends against, and the precheck rules that gate an upload.

All figures here describe the shipped state of v1.0.

## Configuration model

Every pass is switched on through one TOML file, addressed both by the plugin and by the CI script:

```toml
[global]
preset = ""                 # optional built-in preset, applied first
seed = "0xDEADBEEF"         # fixed seed → reproducible output for the same config
verbose = false

[passes.flattening]
enabled = true
probability = 100
```

The plugin is loaded with `-fpass-plugin=libNk.so` and reads the `NK_CONFIG` environment variable; `ci/build-nk.sh` exports `NK=1 NK_CONFIG="$cfg"`. The pre-rename `ENSIA` / `ENSIA_CONFIG` spellings are still accepted as aliases. A `[passes.<key>]` section without `enabled` does not participate in the pipeline, and `enabled = false` is equivalent to omitting the section.

## The 16 passes

| # | Key | Name | What it rewrites | Default |
|---|---|---|---|---|
| 1 | `string_encryption` | String encryption | String constants become ciphertext plus a runtime decrypt call; `anti_dump = true` zeroes the plaintext after use | **on** (core) |
| 2 | `constant_encryption` | Constant encryption | Integer constants globalized into a pool, split into XOR shares, recombined at runtime; `feistel = false` | **on** (core) |
| 3 | `substitution` | Instruction substitution | Replaces single instructions with equivalent sequences | **on** (core) |
| 4 | `bcf` | Bogus control flow | Opaque predicates plus never-taken branches around real blocks | **on** (core) |
| 5 | `split_blocks` | Basic-block splitting | Splits every function into 3 segments; optional `stack_confusion` | **on** (core) |
| 6 | `indirect_branch` | Indirect branching | Jump tables move to the stack, targets are XOR-encrypted | **on** (core) |
| 7 | `flattening` | Control-flow flattening | State machine plus dispatcher; heaviest pass by far | **on** (core, auto-skipped on big TUs) |
| 8 | `anti_debugging` | Anti-debugging (IR level) | Inserts `call __nk_ad_check()` at every function entry | **on** (core) |
| 9 | `mba` | MBA obfuscation | Bit-level identity substitution; large size and time cost | off (experimental) |
| 10 | `chaos_state_machine` | Chaos state machine | Nested dispatchers; not fully validated | off (experimental) |
| 11 | `vector_obfuscation` | Vector obfuscation | Rewrites NEON-width vector ops; not fully validated | off (experimental) |
| 12 | `function_wrapper` | Function wrapping | Renames the original and generates proxy layers | off (experimental) |
| 13 | `function_call_obfuscate` | Call-site obfuscation | Rewrites call sites through an indirection layer | off (experimental) |
| 14 | `anti_hooking` | Anti-hooking | Integrity baseline comparison; unstable in this environment | off (not selectable) |
| 15 | `anti_class_dump` | Anti class dump | Only meaningful for ObjC / Java class metadata | off (not selectable) |
| 16 | `objc` | ObjC obfuscation | No effect on Android binaries | off (not selectable) |

### Core 8

The core 8 are what a normal build runs: two data passes, four control-flow passes, one instruction pass, one injection point.

| Pass | Key parameters | Accepted values | Practical note |
|---|---|---|---|
| `string_encryption` | `probability`, `anti_dump`, `force_content`, `skip_content` | 0–100; bool; regex list; regex list | `anti_dump = true` is mandatory — `false` is known to hang the program in a global constructor |
| `constant_encryption` | `iterations`, `share_count`, `feistel`, `globalize`, `globalize_prob`, `substitute_xor`, `substitute_xor_prob`, `skip_value` | ints; bool | `share_count` is 3–8 in the shipped presets; `feistel` stays `false` |
| `substitution` | `probability`, `iterations` | 0–100; int | Multiplies instruction count; keep `iterations` at 1–2 |
| `bcf` | `probability`, `iterations`, `complexity`, `entropy_chain`, `junk_asm`, `nested`, `create_func` | 0–100; bool | `junk_asm` / `nested` / `create_func` are off in the lowering preset |
| `split_blocks` | `splits`, `stack_confusion` | int; bool | `splits = 3` in `nk-nofold.toml` |
| `indirect_branch` | `use_stack`, `enc_jump_target` | bool | Turns the remaining direct branches indirect after flattening |
| `flattening` | `probability` | 0–100 | Skipped on big TUs; see the degradation tiers in [ARCHITECTURE.md](ARCHITECTURE.md) |
| `anti_debugging` | `probability` | 0–100 | Only the call site is in IR; logic lives in `ci/nk_ad.c` |

### Experimental 5

`mba`, `chaos_state_machine`, `vector_obfuscation`, `function_wrapper`, `function_call_obfuscate` are present, configurable and not validated to production standard. They are exposed because they work on some projects, not because they are safe defaults. `mba` in particular inflates size substantially and lengthens compile time; `function_call_obfuscate` depends on a `dlopen`-capable loader and is the most fragile item in the set.

### Disabled 3

| Key | Why it is off | State |
|---|---|---|
| `anti_hooking` | Integrity baseline mismatch on real devices — the ART runtime patches its own pages during startup | Disabled; not offered as a selectable option in the clients |
| `anti_class_dump` | Only useful with ObjC / Java class metadata | Disabled; not selectable |
| `objc` | Android binaries contain no ObjC metadata | Disabled; not selectable |

## Runtime watchdog: `ci/nk_ad.c`

`nk_ad.c` is a plain C file. It is compiled on its own into `nk_ad_extra.o`, linked into the target, and **never obfuscated** — so its behavior cannot be broken by `flattening` or `bcf`, and it can be debugged with an ordinary debugger. Every check runs inside `__nk_ad_check()`; a hit terminates silently with `_exit(97)`.

Two environment variables control it at runtime:

| Variable | Effect |
|---|---|
| `NK_AD_DEBUG=1` | Writes hit details to stderr and `/data/local/tmp/nk_ad.log` |
| `NK_AD_NO_WLOCK=1` | Suppresses the optional `.text` re-protection (`g_text_rwx_self` guard) |

### Anti-debug

| Check | Mechanism | Signal it reacts to |
|---|---|---|
| `TracerPid` probe | Reads `/proc/self/task/*/TracerPid` across **all** threads, not just the main one | Any tracer that is not the process's own watchdog |
| All-thread scan | Enumerates `/proc/self/task` and inspects each `tid`'s `comm` | Watchdog `nk-wd` is whitelisted by `/proc/<pid>/comm`; anything else counts |
| `prctl(PR_SET_DUMPABLE, 0)` | Makes the process non-dumpable | Prevents core and `/proc/<pid>/mem` access |
| Trace state | `State:\tt` in `/proc/self/status`, or field 3 of `/proc/self/stat` equal to `t` | Stopped / traced process state |
| Hardware breakpoint slots | `perf_event_open` with `HW_BREAKPOINT_X`; `ENOSPC` / `EBUSY` mean the slots are taken | Debuggers that own the four debug registers |
| Watchdog self-check | Child process `nk-wd` tries `PTRACE_SEIZE` on the parent | Success means nobody is attached; failure means somebody is |

### Anti-injection

| Check | Mechanism | Signal it reacts to |
|---|---|---|
| `maps` diff (`nk_maps_hit`) | Parses `/proc/self/maps` with `%n`, compares against a **20-entry** blacklist | `r-x` / `rwx` segments with no file path, `memfd:` mappings, framework paths |
| Anonymous executable segments (`nk_anon_exec`) | Flags executable mappings without a backing file | Injected code regions; ART's `/memfd:jit-cache` is explicitly whitelisted |
| JNI function table (`nk_jni_table_ok`) | The first **8** entries of the `JavaVM` function table must point inside the ART module | A hijacked `JNIEnv` / `JavaVM` table |
| Raw `svc` self-check (`nk_svc_ok`) | Issues `getpid` with an inline `svc #0` only, and compares with the libc result | A syscall-gate hook in libc |
| Direct syscalls | `svc #0` for `openat=56`, `read=63`, `close=57`, `write=64`, `getdents64=61` — no libc in the path | Hooks installed in libc wrappers |
| Uprobe BRK scan (`nk_uprobe_brk`) | Scans the first **8192 B** of its own `.text` for rewritten `BRK` instructions | eDBG / stackplz style uprobes |
| Frida sockets (`nk_frida_socket`) | `/proc/net/tcp{,6}` for LISTEN on **27042 / 27043 / 27052**; `/proc/net/unix` for `@frida` | A Frida server on the standard ports or its abstract socket |
| Frida files (`nk_frida_files`) | Known drop paths and library names | Frida artifacts left on disk |
| Frida maps / comm | `maps` names and per-thread `comm` signatures, `jit` prefixes matched exactly | `frida-jit-agent` and renamed agent threads |
| Trace state (`nk_trace_state`) | See anti-debug | Single-stepping instrumentation |

### Anti-hook / integrity

| Check | Mechanism | Signal it reacts to |
|---|---|---|
| libc prologue encoding | Scans function prologues in libc and key modules; only accepts `B` / `BR` / `BLR` / `ldr+br` that leave the module | Inline hooks placed at function entry |
| Self `.text` hash | FNV hash of the main module's `.text`, sampled **after** the first deep check | Patched code pages |
| Multi-module hash (`nk_tm_collect` / `nk_tm_verify`) | Hash list cut down to main module + `libc` + `libm` + `libdl` | Patching of the runtime itself; `libart` is excluded because ART rewrites its own pages |
| `text_wlock` | Optional `mprotect` of `.text` to read-only; guarded by `g_text_rwx_self` and `g_wlock_tried` | Writable code pages; off by default, some ROMs conflict |

### Anti-emulator / environment

| Check | Mechanism | Signal it reacts to |
|---|---|---|
| Root framework tiering (`nk_root_framework`) | `Xposed` / `LSPosed` / `Zygisk` / `Riru` directories; the mere existence of `/data/adb` counts as root | Rooted or framework-loaded device — scored as a **soft** hit only, never a kill on its own |
| Framework fingerprints | `QBDI`, `unicornTrace`, `substrate` markers | Dynamic-instrumentation frameworks |
| JIT prefix matching | `jit` prefixes matched exactly rather than by substring | Prevents `frida-jit-agent` from hiding behind a legitimate-looking name |

### Detection budget and failure policy

| Property | Value |
|---|---|
| Fail-open | Read failures (permission, race) do not kill the process |
| Kill path | `_exit(97)`, no message, no dialog |
| Soft scoring | Root framework and JNI are soft hits; `g_jni_soft_done` records at most one soft hit so a 4 s polling cycle cannot accumulate three soft hits and self-terminate |
| Hash baseline timing | Deferred to the first deep check; ART patches itself during startup |
| Log destination | `/data/local/tmp/nk_ad.log` when `NK_AD_DEBUG=1` |

## Precheck rules (Android client)

The precheck page runs before any upload. Every rule is visible in the list with the file and the reason, so a failing upload is diagnosable without reading CI logs.

| Rule | Threshold | Level | Why |
|---|---|---|---|
| File size | **> 70 MB** | Fatal — skipped | GitHub rejects it; base64 on the phone would be ~93 MB, far beyond the request body limit |
| Filename contains a space | compile unit only | Fatal | CI splits `SRC_FILES` on whitespace, so the unit is never compiled |
| Filename contains non-ASCII | compile unit only | Fatal | Shell handling inside CI mangles the path |
| File size | > 45 MB | High | base64 expansion makes the upload time out |
| Non-source bulk | > 1 MB and extension in the junk set (`ttf/otf/woff/ttc`, `mp4/mp3/wav/avi/mkv/mov`, `7z/rar/tar/gz/xz/iso/bin`, …) | High | Uploaded but never compiled — pure waiting |
| File size | > 1 MB | Note | Goes through the blob API: works, but slow |
| Non-source extension | any size | Note | Will be uploaded but contributes nothing |
| Compile-unit count | > 60 | High | Too many files in one commit; rate limiting is likely |
| Project total | > 120 MB | High | Both the CI pull and the upload become slow |

Reference limits behind these rules: GitHub's git/blob API accepts single files up to 100 MB, the contents API only 1 MB (used solely for the generated TOML). Measured uploads of 1, 2, 5 and 20 MB all returned HTTP 201 with four-step backoff retries.

## What the output should look like

| Metric | Reference build (run `45041678195`) |
|---|---|
| `build lucifer` step | 41 s |
| Artifact size | 21,145,056 B |
| Artifact sha256 | `6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34` |
| Code section | 16,086,400 B |
| `brk` / `udf` in disassembly | 0 / 0 |
| Plaintext `yz.dujiuyue.cn` occurrences | 0 |
| Plaintext `kami` occurrences | 0 |
