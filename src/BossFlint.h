// fl1nt.dev's hidden level (MIT). See docs/FLINT_FIGHT.md.
#pragma once

void ActBossChar_Enderman(void);

// Draws what the enderman adds to the scene behind the bosses: its crystals'
// healing beams and, in the last phase, its arm reaching in from outside.
void PutFlintExtras(int fx, int fy);

// The page's report that the player's cursor is on the enderman (outside).
void FlintBossStare(void);

// For QA builds: the current phase (1-3), and a way to skip to one.
int FlintBossPhase(void);
int FlintBossRage(void); // in its last form

// QA builds: throw something in off the site now: 0 a piece at screen x a
// that breaks into b pieces, 1 b cursors at screen x a, 2 a stack of b >> 4
// webring buttons flying a (0 left, 2 right), high by b & 15's bits. 3: while
// a, out there it does nothing of its own. 4: a piece falls in at screen x a
// (as with its hand); 5: one comes down from the ceiling there now.
void FlintBossTestThrow(int what, int a, int b);

// The page's pictures of pieces of the site (NpcFlint.h's SITE_*): something
// on its way in uses slot's picture, size w | h << 8 (slot -1: none). what 0:
// a piece, index from its "throw" or "fall"; 1: the index-th button of the
// last stack; 2: picture index (0-5) of the ones ready for pieces falling
// from the ceiling. FlintSiteReload: the page rewrote Npc/NpcSite.pbm.
void FlintSitePicture(int what, int index, int slot, int size);
void FlintSiteReload(void);
void FlintBossSetPhase(int phase);
