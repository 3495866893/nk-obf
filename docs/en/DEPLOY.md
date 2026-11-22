# Deploy to your own repository (step by step)

> Goal: run NK inside **your own GitHub repo**, with one-tap builds from both the desktop and the Android app.
> Everything happens in a browser plus the two GUIs — no local clang / NDK / Python needed.
> Time: ~15 minutes the first time (CI compiles clang once, 40–100 minutes).

---

## 0. What you end up with

```
your repository
├─ .github/workflows/build-obf.yml   → obfuscated binary (Release nk-builds)
├─ .github/workflows/build-apk.yml   → your own Android APK
├─ ci/       build scripts, configs, nk_ad.c runtime watchdog
├─ src/      the pass plugin source (LLVM 21)
├─ kit.tar.gz prebuilt plugin kit (saves ~40 minutes in CI)
└─ tools/ android/  both GUIs
```

Pipeline: `GUI uploads sources → workflow dispatched → clang compiles unit by unit with libNk.so → aarch64 link → artifact published to a Release`.

---

## 1. Prepare the repository (pick one)

### Option A — fork the upstream repo (fastest)

1. Open <https://github.com/3495866893/nk-obf> → **Fork** → your account.
2. **Actions are disabled in forks.** Go to `Settings → Actions → General`, choose
   **Allow all actions and reusable workflows**, then **Save**.
3. On the same page, scroll to **Workflow permissions** → **Read and write permissions** (needed to publish Releases) → **Save**.

### Option B — start from an empty repo

1. GitHub → **New repository** → any name (e.g. `my-obf`) → Create (private or public, both fine).
2. Download the upstream ZIP (Code → Download ZIP), then drag `.github/`, `ci/`, `src/`, `android/`, `tools/`, `kit.tar.gz`, `LICENSE`, `README.md` into **Add file → Upload files** and commit.
   - `.github` is a hidden folder — enable "show hidden items" in your file manager, otherwise Actions will never appear.
3. Same two settings as Option A: enable actions, set **Read and write permissions**.

---

## 2. Create a token (the one the GUI asks for)

> The GUI needs a token that can write code **and** dispatch workflows. Never use your password.

### 2.1 Classic token (simplest)

1. Avatar → **Settings** → **Developer settings** → **Personal access tokens** → **Tokens (classic)**
2. **Generate new token (classic)** → note `nk-build` → expiration 90 days or none
3. Tick exactly two scopes: ✅ `repo`, ✅ `workflow`
4. **Generate token** → copy `ghp_…`
   ⚠️ It is shown once. Paste it somewhere before closing the page.

### 2.2 Fine-grained token (optional, tighter)

| Setting | Value |
|---|---|
| Repository access | Only select repositories → your repo |
| Contents | Read and write |
| Actions | Read and write |
| Workflows | Read and write |

---

## 3. Desktop app: what goes in each field

Launch `NKBuild-1.0-win64.exe` → **Settings**:

| Field | What to put | Example | Where to find it |
|---|---|---|---|
| GitHub Token | the `ghp_…` string | `ghp_ab12…` | step 2 |
| Owner | whoever owns the token | `3495866893` | github.com → your avatar menu |
| Repository | repo name, **no owner prefix** | `nk-obf` | last segment of `github.com/<owner>/<repo>` |
| Branch | default branch | `main` | branch dropdown on the repo home page |
| Workflow file | file name inside `.github/workflows/` | `build-obf.yml` | open the file → Raw → last URL segment |

Hit **Save**. The config lives in `%APPDATA%\NKBuild\config.json` — local only, never uploaded:

```json
{
  "token": "ghp_xxxxxxxxxxxxxxxxxxxx",
  "owner": "3495866893",
  "repo": "nk-obf",
  "branch": "main",
  "workflow": "build-obf.yml",
  "src_root": "projects",
  "passes": ["string_encryption", "constant_encryption", "substitution", "bcf",
             "split_blocks", "indirect_branch", "flattening", "anti_debugging"],
  "last_out": "lucifer"
}
```

---

## 4. Android app: what goes in each field

Bottom tab **Settings** — same five fields, each with an inline hint:

```
GitHub Token     ghp_…            (from step 2)
Owner            3495866893
Repository       nk-obf
Branch           main
Workflow file    build-obf.yml
```

The token is kept in local `SharedPreferences`; it is never uploaded or logged.

---

## 5. First build (the important step)

A brand-new repo has no clang cache, so the first run must build clang:

- **Desktop:** tick **rebuild clang toolchain** (`rebuild_clang=true`) → start.
- **Android:** flip the same switch on the settings page.
- **CLI:**

```bash
gh workflow run build-obf.yml -R <you>/<repo> \
  -f src_dir=projects/probe \
  -f src_files=probe.c \
  -f presets="max mid" \
  -f out_name=hello \
  -f rebuild_clang=true          # first run only
```

That run takes **40–100 minutes** (full LLVM 21 build). Afterwards:

1. clang is stored as the Release asset `llvm-build.tar.zst` (does not consume Actions artifact quota);
2. later runs hit the cache and finish in **1–3 minutes**;
3. the binary lands in **Release `nk-builds`** as `lucifer` (aarch64 PIE).

---

## 6. Self-check list

| Check | Command / place | Expected |
|---|---|---|
| Token valid | `curl -H "Authorization: Bearer $TOK" https://api.github.com/user` | your `login` |
| Repo reachable | `curl -H "Authorization: Bearer $TOK" https://api.github.com/repos/<you>/<repo>` | JSON, not `Not Found` |
| Workflow present | `.../actions/workflows` | `build-obf.yml` listed |
| First build | Actions tab | `build lucifer` green, `成功: xxx.cfg (N bytes)` |
| Artifact | Releases → `nk-builds` | `lucifer`, ~20 MB |

---

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `401 Bad credentials` | expired / truncated / trailing space | regenerate, trim whitespace |
| `403 Resource not accessible` | `workflow` scope missing | re-tick `repo` + `workflow` |
| `404 Not Found` although the repo exists | owner prefix in the repo field, or wrong branch | repo name only; branch `main` |
| 422 `No ref found` | branch name wrong | use the real default branch |
| `Failed to CreateArtifact: Artifact storage quota has been hit` | artifact quota full | publish to Releases (already the default here) |
| Step `guard (cache required)` fails | no clang cache | run once with `rebuild_clang=true` |
| `imgui.cpp` killed with 143 | out of memory on a huge TU | built in: TUs preprocessed >800 KB skip `flattening` |
| `Illegal instruction` at runtime | fallback branch lowered to `brk` | same as above |
| APK won't install | signature changed | uninstall the old build or re-sign with your keystore |

---

## 8. Removing the license gate / changing defaults

- Desktop: delete `import xlverify`, the `_build_license_bar()` call, the `if not self.lic_ok(): …` line in `start()`, and the four methods `lic_login / lic_paste / lic_unbind / lic_ok`; `tools/xlverify.py` can go too.
- Android: delete `XlVerify.kt` and comment out `KamiGate(...)` plus `vm.licAuto(...)` in `MainActivity.kt`.
- Defaults: desktop → `%APPDATA%\NKBuild\config.json`; Android → the default values in `Prefs.kt`.

---

## 9. Licensing

`src/`, `ci/` and `kit.tar.gz` come from **OLLVM-Next** (upstream **AGPL-3.0**): modifications you distribute — including as a network service — must publish the corresponding source.
`tools/`, `android/` and `docs/` are MIT. See the root `LICENSE`.
