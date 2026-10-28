# fl1nt.dev's hidden fight

[fl1nt.dev](https://fl1nt.dev) embeds this build at `/cave`. If a visitor reaches that page by walking into Quote's cave from the site's platformer, the page doesn't start Cave Story: it boots straight into a one-screen arena where the visitor's own sprite fights an enderman. This is an addition to the engine, off by default; the standalone site and normal play are unchanged.

## Engine side (this repository)

- `web_fight(1)`, called before `main()`, skips the opening and title. New games, resets and the death screen's retry (`<INI`) all go to stage 95 instead of the start point. Fight mode first loads the silent track, as the skipped opening would have: with no song loaded, regaining focus restarts music with a zero tempo and the mixer never finishes a buffer.
- Stage 95 (`"Flint"`) is appended to the stage table: tileset `PrtFlint`, map `Flint`, background `bkFlint` (fixed), NPC sheets `Guest` and `Flint`, boss 10.
- Boss 10 (`BossFlint.cpp`) is the enderman: it stalks the player to arm's length, teleports beside (or, if stared at for too long, behind) the player and lunges, and throws its pumpkin (`gBoss[1]`, which can be shot down and breaks on walls and platforms). Below half life it screams and speeds up. Its death event is `#1000`, which should `<BOA0100` (death animation).
- `web_flag(n)` exposes script flags, so the page can see when the fight is won.
- QA builds (`--test-hooks`) add `web_test_boss()` and `web_test_boss_life(life)`.

## Page side (FlintSH/web)

The website writes these into `/data` before calling `main()`. None of the original data files change; `.pbm` files must end in `(C)Pixel` like the originals.

| File | Contents |
| --- | --- |
| `MyChar.pbm` | The player sheet (replaces Quote for this session only) |
| `Stage/PrtFlint.pbm`, `Stage/Flint.pxa` | Tileset and attributes (`0x41` walls, `0x05` platforms) |
| `Stage/Flint.pxm`, `Stage/Flint.pxe`, `Stage/Flint.tsc` | A 20x15 map with the floor at y = 192, no entities, and the script (`#0100` on arrival, `#1000` on the boss's death) |
| `bkFlint.pbm` | Background |
| `Npc/NpcFlint.pbm` | The enderman: 32x48 frames (stand, walk 1, walk 2, stare, carry, throw, fade) facing left on row 1 and right on row 2, and the pumpkin's 16x16 spin frames at y = 96 |

`tests/fight.spec.js` boots fight mode against a QA build with a minimal arena made from the game's own files, and checks the fight starts and the death event runs.
