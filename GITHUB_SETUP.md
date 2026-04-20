# GitHub Actions build setup — step by step

Follow this once, then every time you push code to GitHub you get fresh
`.vst3` (and `.component` on Mac) binaries built on real Windows/Mac/Linux
runners. Download them from the Actions tab, drop into FL Studio, done.

## 0. Prerequisites

- A GitHub account (free is fine) — sign up at https://github.com if needed
- Git installed on your machine — https://git-scm.com/downloads
- The `1017Pack/` folder on your local computer

## 1. Create a new repo on GitHub

1. Go to https://github.com/new
2. **Repository name**: `1017-pack` (or whatever you want)
3. **Visibility**: Private is fine (free accounts get unlimited private repos
   with 2000 CI minutes / month — way more than you'll need)
4. **Do NOT** initialize with README / .gitignore / license — the folder
   already has everything
5. Click **Create repository**
6. On the next page copy the URL that looks like:
   `https://github.com/YOUR_USERNAME/1017-pack.git`

## 2. Push your code

Open a terminal (PowerShell on Windows, Terminal on Mac) in the `1017Pack/`
folder and run:

```bash
git init
git add .
git commit -m "Initial commit — 1017 Pack"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/1017-pack.git
git push -u origin main
```

GitHub will ask you to authenticate. Easiest method:
- Install GitHub CLI: https://cli.github.com/
- Run `gh auth login` once, choose HTTPS, follow the browser flow.
  After that `git push` just works.

Alternative: use a Personal Access Token (Settings → Developer settings
→ Personal access tokens → "Fine-grained tokens" → give it `Contents: RW`
access to this one repo, paste as password when git asks).

## 3. Watch the build

1. Go to `https://github.com/YOUR_USERNAME/1017-pack`
2. Click the **Actions** tab at the top
3. You'll see the "Build 1017 Pack" workflow running — 3 parallel jobs
   (Windows, macOS, Linux)
4. Each takes ~4–8 min on a cold cache, ~2 min once JUCE is cached
5. Wait until all three turn green ✅

## 4. Download the plugin

1. On the Actions tab, click the completed workflow run
2. Scroll to the bottom — you'll see **Artifacts** section with zips:
   - `TRAP_HOUSE-Windows-VST3.zip` ← **what you want for FL**
   - `TRAP_HOUSE-macOS-VST3.zip`
   - `TRAP_HOUSE-macOS-AU.zip`
   - `TRAP_HOUSE-Linux-VST3.zip`
   - (same 4 for LEMONADE)
3. Click the one matching your OS → it downloads a `.zip`
4. Unzip — inside you'll find `TRAP HOUSE.vst3` (a folder on Windows,
   a bundle on Mac)

## 5. Install in FL Studio

### Windows
1. Copy the `TRAP HOUSE.vst3` folder into
   `C:\Program Files\Common Files\VST3\`
   (you may need admin rights — right-click File Explorer, run as admin)
2. Open FL Studio → **Options** → **File settings** → **Manage plugins**
3. Click **Find more plugins** or **Find installed plugins**
4. TRAP HOUSE should appear under the "1017 DSP" manufacturer
5. Favorite it, drop it on a mixer insert, go crazy

### macOS
1. Copy the `TRAP HOUSE.vst3` bundle into
   `~/Library/Audio/Plug-Ins/VST3/` (AU version goes to
   `~/Library/Audio/Plug-Ins/Components/`)
2. Same rescan procedure in FL Studio

## 6. Iterating

Every time you (or Claude in a future session) change the code:

```bash
git add .
git commit -m "Describe what changed"
git push
```

→ A new build starts automatically, new artifacts appear in a few minutes.

## 7. Manual trigger

You can also trigger a build without pushing any code:
- Actions tab → "Build 1017 Pack" on the left → **Run workflow** button
  on the right

Useful when you want a fresh build for the same code (e.g. to re-download
artifacts that expired — free artifacts expire after 90 days).

## Troubleshooting

- **Build fails on first push**: open the red job, scroll through the log.
  Most common issue is a typo in my C++ code. Paste the error back to me in
  a new Claude session with the repo attached — I'll fix it.
- **"No files found" artifact warning**: the build succeeded but the plugin
  wasn't where expected. Check `COPY_PLUGIN_AFTER_BUILD` in the CMakeLists —
  it should be on (it is, by default in my setup).
- **Windows scan doesn't find plugin**: make sure you copied the whole
  `.vst3` folder, not just the inner `.vst3` file. On Windows the outer
  folder IS the plugin.
