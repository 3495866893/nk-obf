# Dependencies and Credits

What NK builds on, and where each piece came from.

## Compiler and toolchain

| Project | Role |
|---|---|
| LLVM 21 | Host for the pass plugin (`libNk.so` via `-fpass-plugin`); sources in `src/` |
| Android NDK r29 | clang 21, lld, sysroot and target headers |
| CMake + Ninja | Plugin build (target `Nk`, artifact `libNk.so`) |

## Desktop GUI

| Project | Role |
|---|---|
| Python 3.10+ | Runtime |
| CustomTkinter | GUI toolkit (cards, rounded corners, sliders) |
| PyInstaller | Single-file executable packaging |
| `tools/xlverify.py` | Self-contained SM4 (GB/T 32907), SM3 (GB/T 32905) and HMAC-SM3 — pure Python, no third-party packages |

## Android GUI

| Project | Role |
|---|---|
| Android Gradle Plugin + Gradle | Build of `android/app`, release signing through `android/keystore/nkbuild.p12` |
| Jetpack Compose + Material3 | Entire UI |
| Haze (`dev.chrisbanes.haze`) | Liquid-glass blur behind the bottom navigation bar (since 1.2); the page backgrounds underneath it are the static skin images added in 1.4 |
| OkHttp | HTTP client for the GitHub API and the license service |
| kotlinx.coroutines | Upload/poll/download concurrency and cancellation |
| AndroidX DocumentFile / FileProvider | Directory picking and APK installation |
| In-house SM4/SM3/HMAC (`XlVerify.kt`) | Kotlin port of the same algorithms |

## Assets

- Skins (5 page backgrounds, chibi avatar, navigation icons): a user-supplied open-source skin pack (`background_home/kernel/settings/superuser/system_module`, `grid_working_card_background`), recompressed to WebP, ~0.46 MB total.
- Button sounds (4 WAV files): synthesized in-project with Python's `wave` module (sweep / two-tone / square / short click); no third-party audio.

## References

- OLLVM / OLLVM-Next style obfuscation (string encryption, control-flow flattening, bogus control flow, instruction substitution).
- Public detection checklists for Frida, Xposed/LSPosed, Zygisk, eDBG/stackplz, QBDI, unicornTrace and similar tooling, which informed the signature tables in `ci/nk_ad.c`.
- KernelSU / APatch skin packs, the visual reference for the Android light theme.

## Build-time dependencies

| Tool | Role | Pinned by |
|---|---|---|
| LLVM 21.1.8 | Compiles every pass; the plugin links against its libs | `LLVM_VER` in `build-obf.yml` / `build-plugin.yml` |
| zstd | Unpacks `llvm-build.tar.zst` (232 MB) on a cache miss | Runner image |
| Android NDK r29 | clang 21, lld, sysroot, bionic headers | `NDK_DIR=/mnt/work/android-ndk-r29` |
| CMake + Ninja | Configure and build target `Nk` → `libNk.so` | `build-plugin.yml` |
| Gradle + AGP | Build `android/app`, sign with `android/keystore/nkbuild.p12` | `android/gradle*` |
| PyInstaller | Freeze `tools/NKBuild.py` into `NKBuild-1.0-win64.exe` (13.35 MB) | Local build script |
| `gh` CLI / workflow API | Publish assets to Release `nk-builds` with `--clobber` | `build-obf.yml` |

## Code written in-project

| Item | Location | Note |
|---|---|---|
| Runtime watchdog | `ci/nk_ad.c` shell + `ci/nk_ad_parts/*.inc` (~85 KB total) | All probes are in-project C: raw `svc #0` syscalls, `/proc` parsers, `perf_event_open` slots. No third-party injection or hooking library |
| Configurations | `ci/*.toml` | `plain.toml`, `only-*.toml`, `nk-nofold.toml`, `tuned.toml`, `nk-ir.toml`, generated `ui-<proj>.toml` |
| Envelope crypto | `tools/xlverify.py` | SM4 (GB/T 32907), SM3 (GB/T 32905) and HMAC-SM3 written from the specifications; no `pycryptodome` dependency |
| Envelope crypto (mobile) | `android/.../XlVerify.kt` | Kotlin port of the same primitives, byte-compatible with the desktop side |
| Build driver | `ci/build-lucifer.sh` (13,344 B) | Per-unit compile loop, large-unit tiering, link line with `--ld-path=$LLD` |
| Plugin loader | `ci/build-nk.sh` (1,746 B) | Exports `NK=1 NK_CONFIG=<toml>` and invokes clang with `-fpass-plugin` |

## Upstream licenses

| Component | Upstream license | Notes |
|---|---|---|
| LLVM 21 | Apache-2.0 with LLVM exceptions | `libNk.so` links against it; redistribution keeps the LLVM notice |
| Android NDK r29 | Apache-2.0 | Toolchain only, not shipped |
| Jetpack Compose / Material3 / AndroidX | Apache-2.0 | Bundled into the APK through Gradle |
| Haze | Apache-2.0 | Single dependency for the blur effect |
| OkHttp / kotlinx.coroutines | Apache-2.0 | Bundled into the APK |
| CustomTkinter | MIT | Bundled into the frozen desktop executable |
| PyInstaller bootloader | GPL-2.0 with a linking exception | The exception is what allows a proprietary single-file executable |
| Skin pack artwork | As shipped with the user-supplied open-source pack | Recompressed to WebP; attribution stays with the pack |

Licenses are quoted from the version pinned in this repository; check the upstream copy before redistributing a modified build.

## Maintenance notes

1. Bumping LLVM means rebuilding `llvm-build.tar.zst`, republishing Release `llvm-build-21.1.8` and rebuilding `libNk.so` — the plugin ABI is not stable across major versions.
2. Bumping the NDK means re-checking the `libc` prologue and `.text` hash baselines that `nk_ad.c` samples at runtime.
3. Bumping Compose or Haze means re-running the on-device UI check on `RMX3888`; the blur layer is the part most sensitive to GPU driver changes.
4. Bumping CustomTkinter means re-freezing the desktop executable and confirming the seven-level output-directory fallback still resolves.
5. Any new third-party file must be added to this page with its license before it is committed.

## License

MIT, see `LICENSE` in the repository root. Third-party components keep their own licenses.

## Licensing (important)

`src/`, `ci/` and `kit.tar.gz` derive from **OLLVM-Next** (upstream AGPL-3.0) and are therefore distributed under AGPL-3.0; redistributing modifications (including as a network service) requires publishing the corresponding source.
`tools/`, `android/` and `docs/` are written for this project and are MIT-licensed. See the root `LICENSE`.
