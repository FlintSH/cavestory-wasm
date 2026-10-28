// Creatures and effects for fl1nt.dev's hidden level, /dev/null. Added by the
// cavestory-wasm project (MIT); see docs/FLINT_FIGHT.md.
#pragma once

#include "NpChar.h"

// NPC codes after the original game's 361. Their table rows are built in
// (FlintNpcTable), so npc.tbl is unchanged.
enum
{
	NPC_FLINT_FIRST = 361,
	NPC_ENDERMITE = 361,
	NPC_STRAY_CURSOR = 362,
	NPC_MOTE_EMITTER = 363,
	NPC_MOTE = 364,
	NPC_ENDER_PEARL = 365,
	NPC_END_GATEWAY = 366,
	NPC_END_ROD = 367,
	NPC_BLOCK_SHARD = 368,
	NPC_PAGE_PIECE = 369,
	NPC_FLINT_COUNT = 9
};

// What a page piece (its direction) is: a piece of fl1nt.dev the enderman
// tore off the site in the last phase.
enum
{
	PIECE_CARD,
	PIECE_LINK,
	PIECE_LOGO,
	PIECE_AVATAR,
	PIECE_STAT,
	PIECE_BADGE,
	PIECE_KINDS
};

// Block shard materials, for SetNpChar's direction argument.
enum
{
	SHARD_END_STONE = 0,
	SHARD_PURPUR = 1,
	SHARD_PAGE = 2
};

// Mote colours, for SetNpChar's direction argument. Add MOTE_TRAVEL for a mote
// that keeps its speed (beams) instead of slowing down (bursts).
enum
{
	MOTE_PURPLE = 0,
	MOTE_PINK = 1,
	MOTE_WHITE = 2,
	MOTE_CYAN = 3,
	MOTE_TRAVEL = 4
};

struct NPC_TABLE;
extern const NPC_TABLE gFlintNpcTable[NPC_FLINT_COUNT];

// A burst of motes, e.g. for a teleport.
void SetMoteBurst(int x, int y, int count, int speed, int colour);

void ActNpc361(NPCHAR *npc);
void ActNpc362(NPCHAR *npc);
void ActNpc363(NPCHAR *npc);
void ActNpc364(NPCHAR *npc);
void ActNpc365(NPCHAR *npc);
void ActNpc366(NPCHAR *npc);
void ActNpc367(NPCHAR *npc);
void ActNpc368(NPCHAR *npc);
void ActNpc369(NPCHAR *npc);
