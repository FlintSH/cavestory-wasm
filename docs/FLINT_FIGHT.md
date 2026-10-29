# fl1nt.dev's hidden level, /dev/null

[fl1nt.dev](https://fl1nt.dev) embeds this build at `/cave`. If a visitor gets there by walking into Quote's cave from the site's platformer, the page doesn't start Cave Story: it boots straight into /dev/null, a short level that ends in a fight with an enderman. Everything here is an addition to the engine and off by default; normal play and the standalone site are unchanged.

## Engine side (this repository)

- `web_fight(1)`, called before `main()`, skips the opening and title. New games, resets and the death screen's retry (`<INI`) all go to stage 95. You arrive at the level's event-90 marker. The level is open to the sky (a maxed Machine Gun can fly you over its walls), so falling out of the bottom of the map is a death in fight mode, with Head.tsc's event 42 ("You were never seen again..."). Fight mode first loads the silent track, as the skipped opening would have: with no song loaded, regaining focus restarts music with a zero tempo and the mixer never finishes a buffer.
- Stage 95 (`"/dev/null"`) is appended to the stage table: tileset `PrtFlint`, map `Flint`, background `bkFlint` with background type 8, NPC sheets `Void` and `Flint`, boss 10.
- Stage 96 (`"lost+found"`), a secret room the level reaches through a hidden End gateway, follows it: tileset `PrtFound`, map `Found`, the same background and NPC sheets, no boss.
- Background type 8 (`Back.cpp`) draws two layers from one 320x240 image: the top half is a sky that tiles and drifts at a quarter of the camera's speed; the bottom half is a band of distant islands (black is see-through) at half speed, riding higher as the camera goes down.
- NPC codes 361-370 (`NpcFlint.cpp`) are the level's creatures and effects. Their table rows are built in and appended when `npc.tbl` has the original 361 entries, so the data files are unchanged:

| Code | | Sheet |
| --- | --- | --- |
| 361 | Endermite: hops at you, like First Cave's critters | `Void`, y 0 |
| 362 | Stray cursor: drifts like First Cave's bats, then dives at you; three colours (by position, or direction 10-12 picks one) | `Void`, y 32 |
| 363 | Mote emitter (invisible): lets motes drift up near the player | |
| 364 | Mote: a speck of purple light (direction picks the colour; +4 keeps its speed) | `Void`, y 128 |
| 365 | Ender Pearl: dropped by the enderman, runs its event when touched | `Void`, y 136 |
| 366 | End gateway: runs its event when touched. Opened by a script (`<CNP`) it appears with a burst and a sound; placed in a map with the alternate direction, it's simply there | `Void`, y 168 |
| 367 | End rod: a flickering light | `Void`, y 152 |
| 368 | Shard: End Stone, purpur or a scrap of the page (direction picks) | `Flint`, y 192; page scraps `Void`, y 64 |
| 369 | A piece of fl1nt.dev the enderman threw in: card, link, logo, avatar, stat or badge (direction picks). Falls, hurts, and breaks into scraps when it lands | `Void`, y 32 |
| 370 | One of the site's 88x31 webring buttons, flung in through the side: spins straight across the room, through its walls, sinking a little (direction 0 flies left, 2 right) | `Void`, x 160, y 0 |

- Boss 10 (`BossFlint.cpp`) is the enderman. It reads its room from the map: event 300's marker is the room's centre, 0x41 tiles are its ceiling, walls, floor and pillars, and the low 0x05 platforms are what it pulls blocks out of (they come back after a while). Event 330 is where it waits, 320 its perch, 310-313 where its crystals grow.
  - The script plays the introduction with `<BOA0010` (turn), `<BOA0020` (glare), `<BOA0030` (scream) and `<BOA0100` (fight).
  - First phase: it stalks you and swipes, teleports beside you, and throws blocks it takes from the platforms. Staring at it for too long makes it scream and appear behind you.
  - At two thirds of its life it teleports to its perch, runs event 1010, and grows crystals that heal it while it throws blocks and summons endermites. Each crystal beams at it and heals it a point about every half second, up to its full life. Destroying them (they explode) knocks it down, stunned.
  - At a third it starts the last phase's cutscene, event 1020. It clears the room (blocks, crystals, endermites), can't be hurt, and gets up off the floor; the script moves it on with `<BOA0320` (it looks out of the screen, at the person playing), `<BOA0330` (it transforms: it rises, flickers into its last form and its life fills back up to 300) and `<BOA0340` (it screams until the screen cracks and climbs out of the room through the middle of the top, where the page has it standing on top of the window). Each of these acts sets itself up and runs as the next number. Without a script running, each moves on to the next by itself after two seconds, so a level without event 1020 can't get stuck. Nothing new starts outside until the script gives control back.
  - Its last form is half as big again: 48x72 frames in the same order as its first sheet's, from `Npc/NpcFlintRage.pbm`, which goes on a surface of its own (`SURFACE_ID_FLINT_RAGE`, 480x288; `PutBossChar` draws a boss part from `surf` when it's set). Its feet stay where they were; its hit box and the reach of its swipe grow with it.
  - The website takes the rest from there (below): the game is pulled into a window on fl1nt.dev, and the enderman is out on the site. From outside it throws a piece of the page in that breaks into two or three as it comes in (code 369), throws a handful of visitors' cursors in (they arrive as stray cursors, code 362, in the colour they had on the site), flings a stack of webring buttons through the room from one side, one after another (code 370, low ones to jump and high ones to stay under, never all high), reaches in with its hand (boss part 11: it hangs over you, slams down, sits, then drags across the floor; hits on the hand hurt the enderman, and after 24 damage it snatches the hand back), or tips the room so you slide. Later on the hand and the tipping come with pieces too. Everything it throws is announced to the page 108 frames (1.8 seconds) before it lands, so the page has time to show it being taken off the site, held up and thrown, and again (`thrown`) 23 frames before, so the page's throw lands with it however fast the game is running. For the last 40 frames motes trickle in where it'll land: down from the ceiling, or in from the wall a button comes through. A trip outside doesn't end until it has all landed. The first time out, it waits two seconds before it starts, so the page can show it typing into the site. After four or five attacks it drops back in, enraged (faster, multiple throws, shadows that dash across the room), and leaves again after eight seconds or 45 damage. At 225, 150 and 75 life it tears more of the site apart, attacks sooner, slams and tips harder, and bits of the room's ceiling fall, some of them pieces of the page.
  - Its death runs event 1000, which should `<BOA0500` for the death; it then drops the Ender Pearl, which falls through the platforms to the floor and runs event 1100 when touched.
- `web_flag(n)` exposes script flags, so the page can see when the level is won and left.
- In fight mode the boss tells the page what it's doing by calling `Module.onFlint(name, a, b)`, if the page set one. Positions are in screen pixels (320x240):

| Event | a, b | |
| --- | --- | --- |
| `reset` | | The fight is set up: a new game, or the retry after a death |
| `rage` | x, y | The last phase's cutscene starts |
| `look` | x, y | It looks out of the screen |
| `transform`, `transformed` | x, y | Its transformation starts, and it's in its last form |
| `break` | x, y | The scream that cracks the screen, where it is |
| `crack` | x, y | Twice more while it screams |
| `out` | x | It has left the room, through the top at x |
| `throw` | kind \| count << 4, x | It's throwing a piece of the page (kind 0-5, code 369's direction), which comes in at x 108 frames later and breaks into count pieces 36px apart |
| `cursor` | colour \| count << 4, x | It's throwing count visitors' cursors (colour 0-2: pink, cyan, orange), which arrive 40px apart around x 108 frames later |
| `button` | direction \| count << 4 \| high << 8, y | It's flinging a stack of count webring buttons in through the side (direction 2: in from the left, flying right), the first 108 frames later and the rest 20 frames apart; bit i of high is set if the i-th is high (26px above y, the low ones' height) |
| `thrown` | what | One of those is 23 frames from landing (what: 0 a piece, 1 cursors, 2 a button) |
| `reach`, `slam`, `unreach` | x | Its hand comes in, slams down, and goes back out |
| `tilt`, `untilt` | direction | It tips the room (1 right, -1 left), and lets go |
| `in` | x | It drops back into the room |
| `hurt` | life, outside | It took damage |
| `crumble` | 1-3 | At 225, 150 and 75 life |
| `stared` | | It noticed you (below) |
| `die` | x, y | Its death, in the last phase |
| `music` | track | The music changed (any time in fight mode; 0 is silence) |

- `web_fight_stare()`: the page calls this when the visitor's cursor rests on the enderman while it's outside. If it isn't busy, and the cutscene is over, it reaches in for you.
- QA builds (`--test-hooks`) add `web_test_boss()` (including its hand and whether it's in its last form), `web_test_boss_life(life)`, `web_test_boss_phase(phase)`, `web_test_boss_part(part, cond)`, `web_test_warp(x, y)`, `web_test_count(code)`, `web_test_find(code)`, `web_test_event(n)`, `web_test_life(life)`, `web_test_exp(exp)`, `web_test_boss_move(x, y)` and `web_test_throw(what, a, b)` (0: throws a piece at screen x a that breaks into b; 1: b cursors at screen x a; 2: a stack of b >> 4 buttons flying a, high by b & 15's bits; 3: while a, out there it does nothing of its own).

## Page side (FlintSH/web)

The website writes these into `/data` before calling `main()`. None of the original data files change. `.pbm` files must end in `(C)Pixel` like the originals, and pure black is see-through.

| File | Contents |
| --- | --- |
| `MyChar.pbm` | The player sheet (replaces Quote for this visit only) |
| `Stage/PrtFlint.pbm`, `Stage/Flint.pxa` | 256x256 tileset and its attributes |
| `Stage/Flint.pxm`, `Stage/Flint.pxe`, `Stage/Flint.tsc` | The map, its entities (including the markers above) and its script |
| `Stage/PrtFound.pbm`, `Stage/Found.pxa`, `Stage/Found.pxm`, `Stage/Found.pxe`, `Stage/Found.tsc` | The same for lost+found |
| `bkFlint.pbm` | 320x240 background for type 8 |
| `Npc/NpcVoid.pbm` | 320x240: the creatures and effects (codes 361-367); the enderman's hand (32x32, open at x 80, flat at x 112) and arm (16x16, x 144) at y 0; the six page pieces at y 32 (x 80-263) and their scraps (8x8, x 80) at y 64 |
| `Npc/NpcFlint.pbm` | 320x240: the enderman's twenty 32x48 poses, ten to a row, facing left then right (y 0-191); its block (16x16, four turns), shards (8x8) and crystal (16x24, four frames, x 128) at y 192 |
| `Npc/NpcFlintRage.pbm` | 480x288: the enderman's last form, its twenty poses as 48x72 frames, ten to a row, facing left then right. No bigger: it's copied into a surface of exactly that size |
| `Face.pbm`, `ItemImage.pbm`, `ArmsItem.tsc` | Faces for the message box, item pictures, and the inventory's descriptions (these replace the originals for this visit only) |

`tests/fight.spec.js` boots fight mode against a QA build with a one-room level made from the game's own files, and checks that you arrive at the marker, the new NPC codes load, a script event takes you to stage 96 and back, the enderman goes through its phases (its crystals heal it; the last phase's cutscene locks you in place, sends the page its events, and transforms it back to full life; then it leaves the room through the middle of the top, throws visitors' cursors, a webring button, a piece that breaks into three and a stack of three buttons, each announced and then `thrown` before it lands, reaches in when stared at, is hurt through its hand and drops back in), its pearl falls from its perch to the floor, and falling out of the map is a death you can retry from, which resets the page and its form. A second test runs the cutscene with no script for it, to show it plays through by itself.
