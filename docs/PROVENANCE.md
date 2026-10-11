# Source and fidelity

This is a maintained browser build of the **CSE2 decompilation**, not an official source release, a fresh decompilation, or the first Cave Story WebAssembly port.

The baseline is [Clownacy's Emscripten branch, mirrored by gameblabla](https://github.com/gameblabla/CSE2/tree/5c65455e603744fc7eb9eea34ad1b608b641de6a), commit `5c65455e603744fc7eb9eea34ad1b608b641de6a` (2020-08-29). The original upstream README is retained in [UPSTREAM-README.md](UPSTREAM-README.md). CSE2 reconstructs the original Windows freeware v1.0.0.6 engine; its browser branch replaces the blocking loops with resumable state machines and replaces Windows/DirectX services with SDL2/FreeType.

## What this repository adds

- A reproducible Emscripten 4.0.21 build for both English and Japanese. Compiles every engine translation unit, including all NPCs, weapons, bosses, scripts, menus, and ending/credits routines. No gameplay features are deliberately stubbed or removed.
- A standalone static website, with a user-initiated start for browser audio, keyboard and touch controls, fullscreen, explicit pause, and pause when the tab is hidden.
- Save restoration before engine startup; serialized IndexedDB writes; original-format save import/export; persistent speedrun records.
- Nonblocking SDL event polling and a safe base-path fallback.
- Browser integration tests, screenshots/recordings, original-asset verification, and GitHub Pages deployment.

The upstream `FIX_BUGS` and `NONPORTABLE` options are disabled. `-fwrapv` preserves signed integer wrapping, and strict aliasing optimizations are disabled. The game runs at its nominal 50 Hz, with its original 320×240 logical viewport. Browser rendering and scheduling, SDL audio resampling, and FreeType font rasterization differ from Windows GDI/DirectSound. Timing depends on browser/host performance.

**This is not proof of 100% behavioral or pixel/audio identity.** Reusing a decompilation and verifying data cannot establish that. Full fidelity would require exhaustive comparisons to the Windows executable, including all paths and endings. The full engine is included; the exact verification performed is recorded in [VERIFICATION.md](VERIFICATION.md).

## Downloaded originals

These are the freeware releases, not Cave Story+ assets:

| Release | Download | Archive SHA-256 | Doukutsu.exe SHA-256 |
| --- | --- | --- | --- |
| Original Japanese v1.0.0.6 | [Studio Pixel](https://studiopixel.jp/binaries/dou_1006.zip) | `68d0dfab0afa2bfb0c6f750638d6a8883576cbbe33f65be8ff51d74272d0eea0` | `e02cc98f5914ec80abe2b6190cf158b889db08ae3fa3eb8074283495e8ad770b` |
| English, Aeon Genesis translation | [Cave Story Tribute Site](https://www.cavestory.one/downloads/cavestoryen.zip) | `aa87fa30bee9b4980640c7e104791354e0f1f6411ee0d45a70af70046aa0685f` | `fa18379fe942ecb5fd69360ede81675ad6bb8b743bef2120305c4b439c732218` |

For each release, all 399 external data files match the downloaded originals byte for byte. All 61 embedded ORG music, WAVE data, and BITMAP credit/title resources match the corresponding PE resources byte for byte (BMP file headers are removed for comparison with the executable's DIB resources). The hash manifests and embedded-resource comparison results are in this directory. Executables are used for verification and are not needed to run the website.

Recheck bundled data with `python3 scripts/verify-assets.py`. Recheck embedded assets after unpacking the originals with:

```sh
python3 scripts/verify-executable-resources.py \
  --english /path/to/CaveStory/Doukutsu.exe \
  --japanese /path/to/doukutsu/Doukutsu.exe
```

Original code and game assets belong to Daisuke “Pixel” Amaya. CSE2's original code/assets are proprietary, despite being publicly available. Its modifications/custom code are MIT licensed; see [LICENCE.txt](../LICENCE.txt). English translation belongs to Aeon Genesis. Upstream font assets are Courier New/MS Gothic; they are not part of an open-source game-asset license. This repository does not relabel these materials as MIT or claim official endorsement.
