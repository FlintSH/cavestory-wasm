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
void FlintBossSetPhase(int phase);
