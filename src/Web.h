// Browser integration added by the cavestory-wasm project (MIT).
#pragma once

// fl1nt.dev's hidden fight: the page sets this before main() runs, and the
// game then boots straight into the arena instead of the opening and title.
// See docs/FLINT_FIGHT.md.
#define FLINT_FIGHT_STAGE 95

// The level marks the player's start with a marker for this event.
#define FLINT_START_EVENT 90

extern int gWebFight;

// Tell the page something happened in the fight: calls Module.onFlint(name,
// a, b) if the page has set it. The hidden level's last phase uses this to
// break out of the canvas (see docs/FLINT_FIGHT.md).
void FlintEmit(const char *name, int a, int b);
