// fl1nt.dev's hidden level (MIT). See docs/FLINT_FIGHT.md.
#pragma once

void ActBossChar_Enderman(void);

// For QA builds: the current phase (1-3), and a way to skip to one.
int FlintBossPhase(void);
void FlintBossSetPhase(int phase);
