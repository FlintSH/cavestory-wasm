# Verification

Checked with Emscripten 4.0.21 and Chromium/Playwright on Linux:

- English and Japanese production builds: the original opening/title sequence and cutscene reach playable Start Point. Actual keyboard input moves and jumps; generated audio contains nonzero samples; Pause stops simulation. Browser screenshots are in this directory.
- Original-format save import, the original save disk's `<SVP>` command, serialized IndexedDB persistence across a reload, save export, and rejection of malformed imports.
- A separate QA build loads all 95 stage entries and runs 100 NPC/boss/character/map simulation ticks in each. It runs the credit script for 25,000 ticks and loads all 17 ending illustrations. This is a smoke test, not a playthrough of every boss or ending. QA functions are excluded from production builds.
- All 399 external files per language match the downloaded freeware originals. All 61 embedded resources per language match the original Windows executables; manifests and the PE comparison report are included.
- The fl1nt.dev integration serves the production game at `/cave` from the site's own origin. Browser checks cover the homepage sprite, hover sound after a user gesture, click navigation, engine startup without QA exports, and mobile placement/navigation. The site's 34 Bun tests pass.

Reproduce browser checks:

```sh
source /path/to/emsdk/emsdk_env.sh
python3 scripts/build.py
python3 scripts/build.py --language en --test-hooks --output build/qa
npm ci
npx playwright install --with-deps chromium
QA_URL=http://localhost:8001 npm test
```

The tests start local HTTP servers. Playwright saves recordings and failure screenshots/traces; GitHub Actions uploads them as `browser-verification`.

These checks do not establish exhaustive parity with the original Windows program. No complete end-to-end playthrough, all-ending comparison, pixel comparison, or bit-identical audio comparison has been performed. See [PROVENANCE.md](PROVENANCE.md) for the source basis and platform differences.
