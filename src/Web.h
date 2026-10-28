// Browser integration added by the cavestory-wasm project (MIT).
#pragma once

// fl1nt.dev's hidden fight: the page sets this before main() runs, and the
// game then boots straight into the arena instead of the opening and title.
// See docs/FLINT_FIGHT.md.
#define FLINT_FIGHT_STAGE 95

extern int gWebFight;
