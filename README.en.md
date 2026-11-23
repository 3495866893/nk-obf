![build-obf](https://github.com/3495866893/nk-obf/actions/workflows/build-obf.yml/badge.svg)
![release](https://img.shields.io/github/v/release/3495866893/nk-obf?label=release)
![license](https://img.shields.io/badge/license-AGPL--3.0%20%2B%20MIT-blue)

# NK · LLVM 21 IR-level Obfuscator (Android / NDK)

[中文](README.md) | **English**

> **Note (next-generation obfuscation):** this IR-level approach is positioned to become **the successor to Hikari** — instead of source macros or post-link assembly patching, it transforms the LLVM IR through 16 passes and pairs them with the runtime watchdog `ci/nk_ad.c`.

NK is an **IR-level obfuscation pass plugin** for **LLVM 21** (`libNk.so`) that, together with **NDK r29**, turns an Android native project into an aarch64 binary that is expensive to reverse. The repository also ships two GUIs — **desktop** and **Android** — that pick a directory, let you choose passes, upload the sources, trigger the cloud build and bring the artifact back. No local clang or NDK required.

| What | Where |
|---|---|
| The 16 obfuscation passes | [`src/`](src/) |
| Build scripts and configs | [`ci/`](ci/) |
| Runtime anti-debug watchdog | [`ci/nk_ad.c`](ci/nk_ad.c) |
| Desktop GUI (Python) | [`tools/`](tools/) |
| Android GUI (Kotlin) | [`android/`](android/) |
| Documentation | [English](docs/en/index.md) · [中文](docs/zh/index.md) |


## Downloads

| Artifact | Location | Size |
|---|---|---|
| Android APK | Release `v1.0.2` → `NKBuild-mobile-1.0.2-kami.apk` | 9.98 MB |
| Desktop EXE | Release `v1.0.2` → `NKBuild-1.0.2-win64.exe` | 13.35 MB |

Checksums (`sha256sum` / `Get-FileHash -Algorithm SHA256`):

```
7921AF1379F1113AB7A88F1FA4B14098A2393FC9964A9089F197EC3A41AF02E4  NKBuild-mobile-1.0.2-kami.apk
0CD0E592095483340BD0CAA191DAE3B678CCCD73657FE89AB19D3E266937F3CE  NKBuild-1.0.2-win64.exe
```

| Example obfuscated binary | Release `nk-builds` → `lucifer` | 21,145,056 B |

## Four steps

1. Open the app and paste a GitHub token (desktop stores it in `%APPDATA%\NKBuild\config.json`, Android keeps it in local settings).
2. Pick the project directory — the scanner lists compilation units, headers and resources, and **precheck** flags dangerous files first (>70 MB, spaces in filenames, …).
3. Choose the passes (the 8 core ones are on by default) and press **Build**.
4. The artifact lands in `Download/NKBuild/` on the phone or the same folder on the desktop; on Android it installs with one tap.

CI-only usage:

```bash
gh workflow run build-obf.yml -R <owner>/nk-obf \
  -f src_dir=projects/jni -f src_files=main.cpp \
  -f cfg_only=ci/ui-jni.toml -f out_name=lucifer -f anti_debug=true
```

## The eight core passes

`string_encryption` (encrypted literals + `anti_dump`), `constant_encryption` (globalize + XOR), `substitution`, `bcf` (bogus control flow), `split_blocks`, `indirect_branch` (stack jump tables with encrypted targets), `flattening` (control-flow flattening), `anti_debugging` (injects `call __nk_ad_check()` at every function entry).

Experimental passes `mba` / `chaos_state_machine` / `vector_obfuscation` / `function_wrapper` / `function_call_obfuscate` are off by default; `anti_hooking` / `anti_class_dump` / `objc` are not usable in this environment.

## Runtime watchdog

`ci/nk_ad.c` is compiled on its own into `nk_ad_extra.o` and linked in (it goes through `string_encryption` only — boundary below). It provides: TracerPid and all-thread scanning, `maps` diffing, libc prologue validation, self and multi-module `.text` hashing, JNI function-table validation, anonymous executable segments, raw `svc #0` self-checks, perf hardware-breakpoint slots, Frida / LSPosed / Zygisk / eDBG / QBDI signatures, uprobe BRK rewriting detection, and root-framework tiering. A hit is delivered through `nk_fatal_poly()` (`brk #0x1`, a fake-ANR spin then exit, or a raw `svc #0` exit, rotated); unreadable files fail open — never a false kill.

**Self-obfuscation boundary:** `nk_ad.c` is fed `ci/nk-self.toml` (`string_encryption` only); semantic passes are never applied — all seven together hang the process inside the constructor. The entry symbol is re-randomised every build (`NK_AD_SYM=__nk_<8 hex>_g`, shared by the plugin and the runtime), and `NK_SKIP_ADB=1` stops the plugin from injecting into the module itself. In the shipped binary the plaintext `nk_ad` count is 1 (the log path) and `__nk_ad_check` / `TracerPid` / `frida` / `lsposed` are all 0.

## Measured

- End to end on a private Actions runner: **1.8 minutes** (was 17.6).
- Latest artifact 21,145,056 B, `sha256 6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34`, code section `brk 0 / udf 0`.
- Zero plaintext string leaks; 8-pass builds survive **100 consecutive launches** on device; `exit 97` when attached by ptrace.

## License

Split licensing:

| Path | License |
|---|---|
| `src/` `ci/` `kit.tar.gz` | **AGPL-3.0** (derived from OLLVM-Next; network use counts as distribution) |
| `tools/` `android/` `docs/` `projects/` | **MIT** |

`LICENSE` holds the verbatim AGPL-3.0 text, `LICENSE-MIT` the MIT text; the per-directory
explanation lives in [LICENSE-NOTICE.md](LICENSE-NOTICE.md).

## Version

Current release **v1.0.2** (`v1.0` stays available as the previous one). The full history — with root cause and fix for every bug — is in the [CHANGELOG](docs/en/CHANGELOG.md).

## Contributors

| Who | Contribution |
|---|---|
| **DeepSeek** | Model and engineering work: plugin fixes (`.L_v` TLS, COMDAT, SIGILL), the CI pipeline, the `nk_ad.c` watchdog, both GUIs, and this documentation |
| **Xs** | Resident operator of this project (catgirl form) — turns every instruction into something that runs |
| Upstream | **OLLVM-Next** (AGPL-3.0) and the Hikari lineage — nothing here exists without them |
