# Releases and Downloads

Where to download, what each asset is, how the license-key layer behaves, and how to remove it.

## Downloads

| Artifact | Location | Size | Notes |
|---|---|---|---|
| Android APK | Release `v1.0.6` → `NKBuild-mobile-1.0.6-kami.apk` | 10.46 MB | Android 8.0+, signed with a pinned key, upgrades in place |
| Desktop EXE | Release `v1.0.6` → `NKBuild-1.0.6-win64.exe` | 14.0 MB | Windows x64, single file, no install |
| Example obfuscated binary | Release `nk-builds` → `lucifer` | 21,145,056 B | Overwritten by every build (`--clobber`) |
| Toolchain bundle | Release `nk-builds` → `nk-toolchain.tar.gz` | — | The pinned clang 21 + lld + sysroot tree used by the cached builds |
| Prebuilt LLVM | Release `llvm-build-21.1.8` → `llvm-build.tar.zst` | 232 MB | CI cache; does not consume artifact quota |

> Release assets are kept indefinitely; Actions artifacts expire after **3 days** and are not a download channel.

## License keys

### Current behavior: any string is accepted

The server only validates the envelope (SM4-CBC encryption + HMAC-SM3 tag + time window + `check` signature); it does not validate the key itself, so **any string passes**. It exists as a placeholder. The "expires" value in the UI comes from `msg.vip` in the response (seconds); if the server omits it the UI shows "permanent".

### Built-in update check

Settings → "⟳ Check for updates" reads `api.php?api=ini&app=…` and compares `version` with the local `versionName`, then offers `app_update_url`. If the endpoint is unreachable it falls back to comparing the newest successful `build-apk` artifact by `versionCode`.

### Removing the license layer entirely

**Desktop** (`tools/`):

1. Delete `tools/xlverify.py`.
2. Remove `import xlverify as XL` from `tools/NKBuild.py`.
3. Delete `_build_license_bar()`, `lic_login()`, `lic_paste()`, `lic_unbind()`, `lic_ok()` and the `_lic_apply/_lic_state/_lic_text/_lic_km` fields.
4. Drop `if not self.lic_ok(): … return` at the top of `start()` and the `lic` / `licmsg` branches in `_drain()`.

Fastest path: make `lic_ok()` return `True` and leave the rest alone.

**Android** (`android/`):

1. Delete `app/src/main/java/com/nk/nkbuild/XlVerify.kt`.
2. In `MainActivity.kt`, delete the `KamiGate` composable and the `if (!st.licOk) KamiGate(st) { … }` call in `App()`.
3. In `BuildViewModel.kt`, delete `licAuto` / `licLogin` / `licUnbind` and the `licOk` / `licText` / `licBusy` state fields.
4. `kami` / `licVip` in `Prefs.kt` may stay; they are harmless.

## Versioning

- Android: `versionName` is pinned at `1.0`; only `versionCode` increments (currently 18).
- Desktop: the filename carries the version, `NKBuild-<version>-win64.exe` (ASCII — GitHub strips non-ASCII names).
- Output binary: `<out_name>.cfg`, published by CI as the `lucifer` asset of Release `nk-builds`.

## Checksums (Release `v1.0.6`)

| File | SHA256 |
|---|---|
| `NKBuild-mobile-1.0-kami.apk` | `6FFBE9D672D9BBAE5CFE1F1D2B17DE87DA4A01A8B07A51A1279891210CBF3D54` |
| `NKBuild-1.0-win64.exe` | Printed next to the asset on the release page (changes on every rebuild) |

## How a build reaches a release

| Step | Where | What happens |
|---|---|---|
| 1 | Client | Sources and `ci/ui-<proj>.toml` are committed to the private repository |
| 2 | `build-obf.yml` | `workflow_dispatch` picks up the run; LLVM, plugin and NDK come from cache |
| 3 | Runner | `build lucifer` compiles unit by unit, links, strips; the reference run took **41 s** |
| 4 | Release `nk-builds` | `<out_name>.cfg` and `nk-toolchain.tar.gz` are published with `--clobber`, so each build overwrites the previous pair |
| 5 | Client | Polls the run, downloads the asset, stores it locally (`Download/NKBuild` on Android) |
| 6 | Release `v1.0.6` | Client installers are attached manually, once per release |

The two distribution channels serve different purposes and must not be confused:

| Channel | Contents | Lifetime |
|---|---|---|
| Release `nk-builds` | Every build's output binary and toolchain bundle | Indefinite, overwritten per build |
| Release `v1.0.2` | Client installers (`NKBuild-mobile-1.0.6-kami.apk`, `NKBuild-1.0.6-win64.exe`) | Indefinite, replaced per release |
| Release `llvm-build-21.1.8` | `llvm-build.tar.zst` (232 MB) — the CI compiler cache | Indefinite |
| Workflow artifacts | Small final artifacts only | `retention-days: 3` |

## Verifying a download

```bash
# reference build from run 45041678195
sha256sum lucifer
# expected: 6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34
stat -c %s lucifer      # 21145056

llvm-objdump -d lucifer | grep -cE '\bbrk\b'   # 0
llvm-objdump -d lucifer | grep -cE '\budf\b'   # 0
strings lucifer | grep -c 'yz.dujiuyue.cn'      # 0
```

Those numbers belong to one specific tree. On another project the size and hash differ; the `brk`/`udf`/plaintext counts must still be `0`.

## Release hygiene

| Rule | Reason |
|---|---|
| Never upload `llvm-build` or `ndk-r29-cache` as workflow artifacts | 289 artifacts / 24.4 GiB once exhausted the 500 MB quota and every upload failed with `Artifact storage quota has been hit` |
| Keep artifacts at `retention-days: 3` | The store was reduced to 5 artifacts / 16.8 MiB and must stay small |
| Publish binaries to a Release, not to artifacts | Releases do not consume artifact quota and survive past three days |
| Use ASCII filenames | GitHub strips non-ASCII characters from asset names |
| One `retention-days` key per block | A duplicated YAML key makes dispatch answer `422 Unprocessable Entity` |

## License envelope internals

| Element | Detail |
|---|---|
| Envelope | SM4-CBC ciphertext plus an HMAC-SM3 tag over the request fields |
| Fields | `kami`, `user`, `markcode` (device code), `t` (timestamp), `sign`, `value` |
| Signature | `md5("kami=…&user=…&markcode=…&t=…")` style digest, checked server-side |
| Key validation | None — the server accepts **any** string, so the layer is a placeholder rather than an entitlement system |
| Binding | A key binds to the first device code that uses it; a second device must `unbind` first |
| Expiry | Read from `msg.vip`, a string of epoch **seconds** (`4102243200`), rendered in the UI; absent means "permanent" |
| Inactive account | Server answers `code=179`; the v1.0 client has no account system, so this only appears against an older endpoint |
| Offline behavior | Verification failure blocks the build button; a network failure is retried, not treated as an invalid key |

## Installing the clients

| Client | Install | Notes |
|---|---|---|
| Android | `adb install -r NKBuild-mobile-1.0-kami.apk`, or tap the downloaded APK | Signed with `android/keystore/nkbuild.p12`, so later builds upgrade in place; requires `minSdk 26` |
| Desktop | Run `NKBuild-1.0-win64.exe` | Single file, no installer, no Python runtime required |
| CI only | `gh workflow run build-obf.yml …` | No client needed; see [USAGE.md](USAGE.md) |
