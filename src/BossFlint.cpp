// The enderman from fl1nt.dev's sidebar, waiting at the bottom of /dev/null.
// Added by the cavestory-wasm project for fl1nt.dev's hidden level (MIT); see
// docs/FLINT_FIGHT.md. Positions use the engine's fixed point, 0x200 per
// pixel; tile n spans n*16-8 to n*16+8.
//
// The website draws the level and the boss reads its arena from the map:
//   event 300's marker   the arena's centre (the camera focuses on it)
//   event 330's marker   where the enderman waits, back turned
//   0x41 tiles           the ceiling, walls, floor and pillars
//   0x05 tiles           platforms; it pulls blocks out of the low ones
//   events 310-313       where its crystals grow (second phase)
//   event 320's marker   where it stands to guard them
//
// The script drives the introduction with <BOA0010 (turn), <BOA0020 (glare),
// <BOA0030 (scream) and <BOA0100 (fight). It runs event 1010 when the
// crystals grow, 1020 when it enrages, and 1000 when it dies; event 1000
// should <BOA0500 for the death. The pearl it drops runs event 1100.
#include "BossFlint.h"

#include <string.h>

#include "WindowsWrapper.h"

#include "Boss.h"
#include "Caret.h"
#include "Flash.h"
#include "Frame.h"
#include "Game.h"
#include "Map.h"
#include "MycParam.h"
#include "MyChar.h"
#include "NpChar.h"
#include "NpcFlint.h"
#include "Sound.h"
#include "TextScr.h"
#include "Triangle.h"

#define PX(n) ((n) * 0x200)

static const int kLife = 300;
static const int kPhase2Life = 200; // crystals
static const int kPhase3Life = 100; // rage

enum
{
	EVENT_ARENA = 300,
	EVENT_CRYSTAL = 310,
	EVENT_PERCH = 320,
	EVENT_START = 330,
	EVENT_DEATH = 1000,
	EVENT_CRYSTALS = 1010,
	EVENT_RAGE = 1020,
	EVENT_PEARL = 1100
};

// Boss parts.
enum
{
	PART_BLOCK = 1, // 1-3: thrown blocks
	BLOCKS = 3,
	PART_SWIPE = 4, // the reach of its arm
	PART_CRYSTAL = 5, // 5-8
	CRYSTALS = 4,
	PART_SHADOW = 9, // 9-10: shadows that dash across the arena
	SHADOWS = 2
};

// Npc/NpcFlint.pbm: 32x48 frames, ten to a row. Frames 0-9 face left at y = 0
// and right at y = 48; frames 10-19 face left at y = 96 and right at y = 144.
// Below them, at y = 192: the block (16x16, four turns), block shards (8x8,
// drawn by NpcFlint.cpp) and the crystal (16x24, four frames, at x = 128).
enum
{
	FRAME_STAND,
	FRAME_REACH,
	FRAME_WALK1,
	FRAME_WALK2,
	FRAME_WALK3,
	FRAME_WALK4,
	FRAME_CARRY,
	FRAME_CARRY_WALK1,
	FRAME_CARRY_WALK2,
	FRAME_THROW,
	FRAME_WINDUP,
	FRAME_SWIPE,
	FRAME_SCREAM,
	FRAME_STARE,
	FRAME_FADE1,
	FRAME_FADE2,
	FRAME_FADE3,
	FRAME_STUN,
	FRAME_HURT,
	FRAME_FALL,
	FRAME_HIDDEN
};

enum
{
	ACT_SETUP = 0,
	ACT_WAIT = 1,
	ACT_TURN = 10,
	ACT_GLARE = 20,
	ACT_SCREAM = 30,
	ACT_FIGHT = 100,
	ACT_THINK = 110,
	ACT_STALK = 120,
	ACT_WINDUP = 130,
	ACT_SWIPE = 131,
	ACT_RECOVER = 132,
	ACT_VANISH = 140,
	ACT_HIDDEN = 141,
	ACT_APPEAR = 142,
	ACT_REACH = 150,
	ACT_CARRY = 151,
	ACT_THROW = 152,
	ACT_PROVOKED = 160,
	ACT_CRYSTALS = 200,
	ACT_GUARD = 210,
	ACT_FALL = 219,
	ACT_STUNNED = 220,
	ACT_RAGE = 300,
	ACT_SHADOWS = 310,
	ACT_DIE = 500,
	ACT_DYING = 501
};

// What to do after reappearing.
enum
{
	NEXT_THINK,
	NEXT_STRIKE,
	NEXT_STEAL,
	NEXT_CRYSTALS
};

struct Stolen
{
	int x;
	int y;
	unsigned char tile;
	int wait; // until it's put back; 0 while the slot is free, -1 while it sits in the platform
};

static struct
{
	int left, right, floor; // inner faces and floor surface, fixed point
	int startX, startY, perchX, perchY;
	int crystalX[CRYSTALS], crystalY[CRYSTALS];
	int crystalCount;
	Stolen platform[64];
	int platformCount;
	int phase; // 1, 2 (crystals) or 3 (rage)
	int moves; // since the last teleport
	int next;
	int destX, destY;
	int stare;
	int stareCooldown;
	int stealing; // index into platform
	int throws; // blocks left to throw in a volley
	BOOL crystalAlive[CRYSTALS];
	BOOL blockFlying[BLOCKS];
	int blockX[BLOCKS], blockY[BLOCKS];
	BOOL stunned; // has been knocked off its perch this fight
} arena;

static BOOL Solid(unsigned char attribute)
{
	return attribute == 0x41 || attribute == 0x05 || attribute == 0x43;
}

static int TileOf(int v)
{
	return (v / 0x200 + 8) / 16;
}

static void SetFrame(NPCHAR *npc, int frame)
{
	if (frame == FRAME_HIDDEN)
	{
		npc->rect.left = npc->rect.right = npc->rect.top = npc->rect.bottom = 0;
		return;
	}

	npc->rect.left = (frame % 10) * 32;
	npc->rect.top = (frame / 10) * 96 + (npc->direct == 0 ? 0 : 48);
	npc->rect.right = npc->rect.left + 32;
	npc->rect.bottom = npc->rect.top + 48;
}

static BOOL FindMarker(int event, int *x, int *y)
{
	for (int i = 0; i < NPC_MAX; i++)
	{
		if ((gNPC[i].cond & 0x80) && gNPC[i].code_event == event)
		{
			*x = gNPC[i].x;
			*y = gNPC[i].y;
			return TRUE;
		}
	}

	return FALSE;
}

// The first surface at or below (x, y), as the y of a 48px enderman standing on it.
static int StandY(int x, int y)
{
	int tx = TileOf(x);
	for (int ty = TileOf(y); ty < gMap.length; ty++)
		if (Solid(GetAttribute(tx, ty)) && !Solid(GetAttribute(tx, ty - 1)))
			return PX(ty * 16 - 8 - 24);

	return arena.floor - PX(24);
}

static void ReadArena(NPCHAR *npc)
{
	memset(&arena, 0, sizeof(arena));
	arena.phase = 1;

	int cx = npc->x, cy = npc->y;
	FindMarker(EVENT_ARENA, &cx, &cy);
	int tx = TileOf(cx), ty = TileOf(cy);

	int floorRow = ty, ceilingRow = ty;
	while (floorRow < gMap.length - 1 && GetAttribute(tx, floorRow) != 0x41)
		++floorRow;
	while (ceilingRow > 0 && GetAttribute(tx, ceilingRow) != 0x41)
		--ceilingRow;

	// Walls: scan just under the ceiling, clear of the door and the pillars.
	int leftCol = tx, rightCol = tx;
	while (leftCol > 0 && GetAttribute(leftCol, ceilingRow + 1) != 0x41)
		--leftCol;
	while (rightCol < gMap.width - 1 && GetAttribute(rightCol, ceilingRow + 1) != 0x41)
		++rightCol;

	arena.left = PX(leftCol * 16 + 8);
	arena.right = PX(rightCol * 16 - 8);
	arena.floor = PX(floorRow * 16 - 8);

	// Platforms it can reach from the floor (their underside at most 72px up).
	for (int y = ceilingRow + 1; y < floorRow; y++)
	{
		for (int x = leftCol + 1; x < rightCol; x++)
		{
			if (GetAttribute(x, y) != 0x05 || arena.platformCount >= 64)
				continue;
			if (arena.floor - PX(y * 16 + 8) > PX(72))
				continue;

			Stolen *s = &arena.platform[arena.platformCount++];
			s->x = x;
			s->y = y;
			s->tile = gMap.data[x + y * gMap.width];
			s->wait = -1;
		}
	}

	for (int i = 0; i < CRYSTALS; i++)
		if (FindMarker(EVENT_CRYSTAL + i, &arena.crystalX[arena.crystalCount], &arena.crystalY[arena.crystalCount]))
			++arena.crystalCount;

	arena.perchX = (arena.left + arena.right) / 2;
	arena.perchY = arena.floor - PX(24);
	if (FindMarker(EVENT_PERCH, &arena.perchX, &arena.perchY))
		arena.perchY = StandY(arena.perchX, arena.perchY);

	arena.startX = arena.left + (arena.right - arena.left) * 2 / 3;
	arena.startY = arena.floor - PX(24);
	if (FindMarker(EVENT_START, &arena.startX, &arena.startY))
		arena.startY = StandY(arena.startX, arena.startY);
}

static void FacePlayer(NPCHAR *npc)
{
	npc->direct = gMC.x < npc->x ? 0 : 2;
}

static void Clamp(NPCHAR *npc)
{
	if (npc->x < arena.left + PX(8))
		npc->x = arena.left + PX(8);
	if (npc->x > arena.right - PX(8))
		npc->x = arena.right - PX(8);
}

static void Tremble(NPCHAR *npc)
{
	npc->x += npc->act_wait % 2 ? PX(1) : -PX(1);
}

static void Vulnerable(NPCHAR *npc, BOOL on)
{
	npc->bits &= ~(npc_shootable | npc_invulnerable);
	if (on)
		npc->bits |= npc_shootable;
}

// Endermen hate being looked at.
static BOOL StaredAt(NPCHAR *npc)
{
	int dx = npc->x - gMC.x;
	int dy = npc->y - gMC.y;
	if (dy < -PX(56) || dy > PX(56) || dx < -PX(220) || dx > PX(220))
		return FALSE;

	return (gMC.direct == 2 && dx > 0) || (gMC.direct == 0 && dx < 0);
}

static int Speed(int base)
{
	return arena.phase == 3 ? base * 3 / 2 : arena.phase == 2 ? base * 5 / 4 : base;
}

static int Ticks(int base)
{
	return arena.phase == 3 ? base / 2 : arena.phase == 2 ? base * 3 / 4 : base;
}

// ------------------------------------------------------------------ blocks

static void Shatter(int x, int y)
{
	for (int i = 0; i < 6; i++)
		SetNpChar(NPC_BLOCK_SHARD, x, y, Random(-0x300, 0x300), Random(-0x600, -0x100), 0, NULL, 0x100);

	SetDestroyNpChar(x, y, PX(6), 3);
	PlaySoundObject(12, 1);
}

static void ThrowBlock(NPCHAR *npc)
{
	for (int i = 0; i < BLOCKS; i++)
	{
		NPCHAR *block = &gBoss[PART_BLOCK + i];
		if (block->cond & 0x80)
			continue;

		memset(block, 0, sizeof(*block));
		block->cond = 0x80;
		block->direct = npc->direct;
		block->x = npc->x + (npc->direct == 0 ? -PX(8) : PX(8));
		block->y = npc->y - PX(30);

		// An arc that comes down on the player.
		int dx = gMC.x - block->x;
		int t = (dx < 0 ? -dx : dx) / 0x380;
		if (t < 26)
			t = 26;
		if (t > 56)
			t = 56;
		block->xm = dx / t;
		block->ym = (gMC.y - block->y) / t - 0x20 * t / 2;
		if (block->ym < -0x900)
			block->ym = -0x900;

		block->life = 2;
		block->damage = 2;
		block->hit_voice = 52;
		block->destroy_voice = 12;
		block->bits = npc_shootable | npc_ignoreSolid;
		block->hit.front = block->hit.back = block->hit.top = block->hit.bottom = PX(6);
		block->view.front = block->view.back = block->view.top = block->view.bottom = PX(8);
		arena.blockFlying[i] = TRUE;
		PlaySoundObject(39, 1);
		return;
	}
}

static void ActBlocks(void)
{
	for (int i = 0; i < BLOCKS; i++)
	{
		NPCHAR *block = &gBoss[PART_BLOCK + i];

		if ((block->cond & 0x80) == 0)
		{
			// Shot down.
			if (arena.blockFlying[i])
				Shatter(arena.blockX[i], arena.blockY[i]);
			arena.blockFlying[i] = FALSE;
			continue;
		}

		block->ym += 0x20;
		if (block->ym > 0x700)
			block->ym = 0x700;
		block->x += block->xm;
		block->y += block->ym;
		arena.blockX[i] = block->x;
		arena.blockY[i] = block->y;

		if (++block->ani_wait > 3)
		{
			block->ani_wait = 0;
			block->ani_no = (block->ani_no + (block->xm < 0 ? 3 : 1)) % 4;
		}

		block->rect.left = block->ani_no * 16;
		block->rect.top = 192;
		block->rect.right = block->rect.left + 16;
		block->rect.bottom = 208;

		int px = block->x - gMC.x, py = block->y - gMC.y;
		BOOL hitPlayer = px > -PX(12) && px < PX(12) && py > -PX(14) && py < PX(14);
		// Like bullets, blocks pass through platforms (0x05); walls, the floor
		// and ledges break them.
		unsigned char under = GetAttribute(TileOf(block->x), TileOf(block->y));
		if (under == 0x41 || under == 0x43 || block->y > arena.floor + PX(32) || hitPlayer)
		{
			if (hitPlayer && !(gMC.cond & 2))
				DamageMyChar(block->damage);

			Shatter(block->x, block->y);
			block->cond = 0;
			arena.blockFlying[i] = FALSE;
		}
	}
}

// Platform blocks it took come back after a while (not on top of you).
static void RestorePlatforms(BOOL all)
{
	for (int i = 0; i < arena.platformCount; i++)
	{
		Stolen *s = &arena.platform[i];
		if (s->wait <= 0)
			continue;

		if (!all && --s->wait > 0)
			continue;

		int px = gMC.x - PX(s->x * 16), py = gMC.y - PX(s->y * 16);
		if (!all && px > -PX(16) && px < PX(16) && py > -PX(20) && py < PX(20))
		{
			s->wait = 10;
			continue;
		}

		ChangeMapParts(s->x, s->y, s->tile);
		SetMoteBurst(PX(s->x * 16), PX(s->y * 16), 4, 0x300, MOTE_PURPLE);
		s->wait = -1;
	}
}

static int PickPlatform(void)
{
	int best = -1, bestScore = 0x7FFFFFFF;
	for (int i = 0; i < arena.platformCount; i++)
	{
		if (arena.platform[i].wait != -1)
			continue;

		// The block nearest you, give or take.
		int dx = PX(arena.platform[i].x * 16) - gMC.x;
		int score = (dx < 0 ? -dx : dx) + PX(Random(0, 96));
		if (score < bestScore)
		{
			bestScore = score;
			best = i;
		}
	}

	return best;
}

// ------------------------------------------------------------------ crystals

static void GrowCrystals(void)
{
	for (int i = 0; i < arena.crystalCount; i++)
	{
		NPCHAR *c = &gBoss[PART_CRYSTAL + i];
		memset(c, 0, sizeof(*c));
		c->cond = 0x80;
		c->x = arena.crystalX[i];
		c->y = arena.crystalY[i] - PX(4);
		c->tgt_y = c->y;
		c->life = 14;
		c->hit_voice = 31;
		c->destroy_voice = 35;
		c->size = 2;
		c->bits = npc_shootable | npc_ignoreSolid | npc_showDamage;
		c->hit.front = c->hit.back = PX(6);
		c->hit.top = PX(8);
		c->hit.bottom = PX(12);
		c->view.front = c->view.back = PX(8);
		c->view.top = c->view.bottom = PX(12);
		c->count1 = i * 40;
		arena.crystalAlive[i] = TRUE;
		SetMoteBurst(c->x, c->y, 14, 0x600, MOTE_PINK);
		SetCaret(c->x, c->y, 12, 0);
	}

	PlaySoundObject(103, 1);
}

static int ActCrystals(NPCHAR *boss)
{
	int alive = 0;
	for (int i = 0; i < arena.crystalCount; i++)
	{
		NPCHAR *c = &gBoss[PART_CRYSTAL + i];

		if ((c->cond & 0x80) == 0)
		{
			if (arena.crystalAlive[i])
			{
				// Destroyed: it goes up with a bang.
				arena.crystalAlive[i] = FALSE;
				SetDestroyNpChar(c->x, c->y, PX(12), 10);
				SetMoteBurst(c->x, c->y, 24, 0x800, MOTE_PINK);
				SetQuake(20);
				PlaySoundObject(35, 1);
				int dx = gMC.x - c->x, dy = gMC.y - c->y;
				if (dx > -PX(36) && dx < PX(36) && dy > -PX(36) && dy < PX(36))
					DamageMyChar(2);
			}
			continue;
		}

		++alive;
		c->count1 += 3;
		c->y = c->tgt_y + GetSin((unsigned char)c->count1) * 2;

		if (++c->ani_wait > 4)
		{
			c->ani_wait = 0;
			c->ani_no = (c->ani_no + 1) % 4;
		}

		c->rect.left = 128 + c->ani_no * 16;
		c->rect.top = 192;
		c->rect.right = c->rect.left + 16;
		c->rect.bottom = 216;

		// A beam of motes to the enderman, healing it.
		if (boss->act_no == ACT_GUARD && ++c->act_wait % 4 == 0)
		{
			unsigned char deg = GetArktan(c->x - boss->x, c->y - (boss->y - PX(8)));
			SetNpChar(NPC_MOTE, c->x, c->y, GetCos(deg) * 4, GetSin(deg) * 4, MOTE_PINK | MOTE_TRAVEL, NULL, 0x100);
		}
	}

	return alive;
}

// ------------------------------------------------------------------ shadows

static void SendShadows(void)
{
	for (int i = 0; i < SHADOWS; i++)
	{
		NPCHAR *s = &gBoss[PART_SHADOW + i];
		memset(s, 0, sizeof(*s));
		s->cond = 0x80;
		s->direct = i == 0 ? 2 : 0;
		s->x = i == 0 ? arena.left + PX(12) : arena.right - PX(12);
		s->y = StandY(s->x, gMC.y - PX(8));
		if (i == 1 && s->y > gMC.y + PX(40))
			s->y = arena.floor - PX(24);
		s->life = 1;
		s->damage = 3;
		s->destroy_voice = 29;
		s->size = 2;
		s->bits = npc_shootable | npc_ignoreSolid;
		s->hit.front = s->hit.back = PX(6);
		s->hit.top = PX(20);
		s->hit.bottom = PX(24);
		s->view.front = s->view.back = PX(16);
		s->view.top = s->view.bottom = PX(24);
		SetMoteBurst(s->x, s->y, 10, 0x500, MOTE_PURPLE);
	}

	PlaySoundObject(29, 1);
}

static void ActShadows(void)
{
	for (int i = 0; i < SHADOWS; i++)
	{
		NPCHAR *s = &gBoss[PART_SHADOW + i];
		if ((s->cond & 0x80) == 0)
			continue;

		++s->act_wait;
		if (s->act_wait > 36)
		{
			s->xm = s->direct == 0 ? -0x700 : 0x700;
			s->x += s->xm;
			if (s->act_wait % 3 == 0)
				SetNpChar(NPC_MOTE, s->x, s->y + PX(Random(-16, 16)), 0, 0, MOTE_PURPLE, NULL, 0x100);
		}

		if (s->x < arena.left || s->x > arena.right || s->act_wait > 140)
		{
			SetMoteBurst(s->x, s->y, 8, 0x400, MOTE_PURPLE);
			s->cond = 0;
			continue;
		}

		// Flickering, and only dangerous once it runs.
		s->damage = s->act_wait > 36 ? 2 : 0;
		BOOL shown = s->act_wait > 36 || s->act_wait % 4 < 2;
		int frame = s->act_wait > 36 ? FRAME_FADE1 : FRAME_STARE;
		if (shown)
			SetFrame(s, frame);
		else
			SetFrame(s, FRAME_HIDDEN);
	}
}

// ------------------------------------------------------------------ moves

static void StartTeleport(NPCHAR *npc, int x, int y, int next)
{
	arena.destX = x;
	arena.destY = y;
	arena.next = next;
	arena.moves = 0;
	npc->act_no = ACT_VANISH;
	npc->act_wait = 0;
	npc->xm = 0;
	PlaySoundObject(29, 1);
	SetMoteBurst(npc->x, npc->y, 12, 0x500, MOTE_PURPLE);
}

// Beside the player, or behind them if they've been staring.
static void TeleportToPlayer(NPCHAR *npc, BOOL behind)
{
	int side;
	if (behind)
		side = gMC.direct == 0 ? 1 : -1;
	else
		side = Random(0, 1) ? 1 : -1;

	int x = gMC.x + side * PX(34);
	if (x < arena.left + PX(12) || x > arena.right - PX(12))
		x = gMC.x - side * PX(34);
	if (x < arena.left + PX(12))
		x = arena.left + PX(12);
	if (x > arena.right - PX(12))
		x = arena.right - PX(12);

	StartTeleport(npc, x, StandY(x, gMC.y - PX(8)), NEXT_STRIKE);
}

static void Think(NPCHAR *npc)
{
	FacePlayer(npc);
	npc->xm = 0;
	npc->act_wait = 0;

	if (arena.phase == 1 && npc->life <= kPhase2Life)
	{
		StartTeleport(npc, arena.perchX, arena.perchY, NEXT_CRYSTALS);
		return;
	}

	if (arena.phase == 2 && arena.stunned && npc->life <= kPhase3Life)
	{
		arena.phase = 3;
		npc->act_no = ACT_RAGE;
		return;
	}

	int dx = gMC.x - npc->x;
	if (dx < 0)
		dx = -dx;

	int roll = Random(0, 99);
	if (arena.phase == 3 && roll < 22 && !(gBoss[PART_SHADOW].cond & 0x80))
	{
		npc->act_no = ACT_SHADOWS;
	}
	else if (dx < PX(44))
	{
		npc->act_no = ACT_WINDUP;
	}
	else if (roll < 30 || arena.moves >= 3)
	{
		TeleportToPlayer(npc, FALSE);
	}
	else if (roll < 58 && arena.platformCount > 0)
	{
		arena.stealing = PickPlatform();
		if (arena.stealing < 0)
		{
			npc->act_no = ACT_STALK;
		}
		else
		{
			Stolen *s = &arena.platform[arena.stealing];
			s->wait = 0x7FFF; // spoken for
			int x = PX(s->x * 16) + (gMC.x < PX(s->x * 16) ? PX(10) : -PX(10));
			StartTeleport(npc, x, arena.floor - PX(24), NEXT_STEAL);
		}
	}
	else
	{
		npc->act_no = ACT_STALK;
	}

	++arena.moves;
}

static void Strike(NPCHAR *npc, BOOL on)
{
	NPCHAR *swipe = &gBoss[PART_SWIPE];
	if (!on)
	{
		swipe->cond = 0;
		return;
	}

	memset(swipe, 0, sizeof(*swipe));
	swipe->cond = 0x80;
	// Contact is checked against a small box at the player's centre, so the
	// reach runs from above its head down to just over the floor.
	swipe->x = npc->x + (npc->direct == 0 ? -PX(20) : PX(20));
	swipe->y = npc->y;
	swipe->damage = 3;
	swipe->bits = npc_ignoreSolid;
	swipe->hit.front = swipe->hit.back = PX(14);
	swipe->hit.top = PX(22);
	swipe->hit.bottom = PX(22);
}

static void Die(NPCHAR *npc)
{
	Vulnerable(npc, FALSE);
	npc->damage = 0;
	npc->xm = 0;
	npc->act_no = ACT_DYING;
	npc->act_wait = 0;
	Strike(npc, FALSE);

	for (int i = 0; i < BLOCKS; i++)
	{
		if (gBoss[PART_BLOCK + i].cond & 0x80)
			Shatter(gBoss[PART_BLOCK + i].x, gBoss[PART_BLOCK + i].y);
		gBoss[PART_BLOCK + i].cond = 0;
		arena.blockFlying[i] = FALSE;
	}
	for (int i = 0; i < SHADOWS; i++)
		gBoss[PART_SHADOW + i].cond = 0;
	for (int i = 0; i < CRYSTALS; i++)
	{
		gBoss[PART_CRYSTAL + i].cond = 0;
		arena.crystalAlive[i] = FALSE;
	}

	// Its endermites go with it.
	for (int i = 0; i < NPC_MAX; i++)
	{
		if ((gNPC[i].cond & 0x80) && gNPC[i].code_char == NPC_ENDERMITE && gNPC[i].x > arena.left && gNPC[i].x < arena.right && gNPC[i].y < arena.floor)
		{
			SetMoteBurst(gNPC[i].x, gNPC[i].y, 6, 0x400, MOTE_PURPLE);
			gNPC[i].cond = 0;
		}
	}

	RestorePlatforms(TRUE);
	SetQuake(150);
}

// The pearl falls through the platforms to the floor (its tgt_y), so it never
// ends up somewhere the player can't reach.
static void DropPearl(NPCHAR *npc)
{
	SetNpChar(NPC_ENDER_PEARL, npc->x, npc->y - PX(8), 0, -0x500, 0, NULL, 0x20);
	for (int i = 0; i < NPC_MAX; i++)
	{
		if ((gNPC[i].cond & 0x80) && gNPC[i].code_char == NPC_ENDER_PEARL && gNPC[i].code_event == 0)
		{
			gNPC[i].code_event = EVENT_PEARL;
			gNPC[i].tgt_y = arena.floor;
			break;
		}
	}
}

static int Endermites(void)
{
	int count = 0;
	for (int i = 0; i < NPC_MAX; i++)
		if ((gNPC[i].cond & 0x80) && gNPC[i].code_char == NPC_ENDERMITE && gNPC[i].x > arena.left && gNPC[i].x < arena.right && gNPC[i].y < arena.floor)
			++count;
	return count;
}

static void Setup(NPCHAR *npc)
{
	ReadArena(npc);
	npc->cond = 0x80;
	npc->x = arena.startX;
	npc->y = arena.startY;
	npc->direct = 2;
	npc->life = kLife;
	npc->size = 3;
	npc->hit_voice = 52;
	npc->destroy_voice = 72;
	npc->code_event = EVENT_DEATH;
	npc->bits = npc_eventDie | npc_showDamage;
	npc->hit.front = npc->hit.back = PX(6);
	npc->hit.top = PX(20);
	npc->hit.bottom = PX(24);
	npc->view.front = npc->view.back = PX(16);
	npc->view.top = npc->view.bottom = PX(24);
	npc->act_no = ACT_WAIT;
}

void ActBossChar_Enderman(void)
{
	NPCHAR *npc = &gBoss[0];

	// A script can <BOA in the same frame the stage loads, before this has
	// ever run: set up first, then take the act it asked for.
	if (npc->size == 0)
	{
		int act = npc->act_no;
		Setup(npc);
		if (act != ACT_SETUP)
			npc->act_no = act;
	}

	// Keep each phase from being skipped by a burst of damage.
	if (npc->act_no >= ACT_FIGHT && npc->act_no < ACT_DIE)
	{
		if (arena.phase == 1 && npc->life < kPhase2Life)
			npc->life = kPhase2Life;
		if (arena.phase == 2 && npc->life < kPhase3Life)
			npc->life = kPhase3Life;
	}

	switch (npc->act_no)
	{
		case ACT_SETUP:
			Setup(npc);
			// Fallthrough
		case ACT_WAIT:
			// Back turned, gazing into the void.
			SetFrame(npc, FRAME_STAND);
			if (++npc->act_wait % 90 == 0)
				SetNpChar(NPC_MOTE, npc->x + PX(Random(-6, 6)), npc->y - PX(16), 0, 0, MOTE_PURPLE, NULL, 0x100);
			break;

		case ACT_TURN:
			npc->act_no = ACT_TURN + 1;
			npc->act_wait = 0;
			// Fallthrough
		case ACT_TURN + 1:
			FacePlayer(npc);
			SetFrame(npc, npc->act_wait < 6 ? FRAME_HURT : FRAME_STAND);
			++npc->act_wait;
			break;

		case ACT_GLARE:
			npc->act_no = ACT_GLARE + 1;
			npc->act_wait = 0;
			// Fallthrough
		case ACT_GLARE + 1:
			FacePlayer(npc);
			SetFrame(npc, FRAME_STARE);
			Tremble(npc);
			if (npc->act_wait++ == 0)
				PlaySoundObject(108, 1);
			break;

		case ACT_SCREAM:
			npc->act_no = ACT_SCREAM + 1;
			npc->act_wait = 0;
			SetQuake(40);
			PlaySoundObject(26, 1);
			PlaySoundObject(52, 1);
			SetMoteBurst(npc->x, npc->y - PX(16), 18, 0x700, MOTE_PURPLE);
			// Fallthrough
		case ACT_SCREAM + 1:
			SetFrame(npc, FRAME_SCREAM);
			Tremble(npc);
			++npc->act_wait;
			break;

		case ACT_FIGHT:
			Vulnerable(npc, TRUE);
			npc->damage = 2;
			arena.stareCooldown = 200;
			npc->act_no = ACT_THINK;
			// Fallthrough
		case ACT_THINK:
			SetFrame(npc, FRAME_STAND);
			if (++npc->act_wait > Ticks(16))
				Think(npc);
			break;

		case ACT_STALK:
		{
			// Long strides toward you, stopping at arm's length.
			FacePlayer(npc);
			int dx = gMC.x - npc->x;
			npc->xm = npc->direct == 0 ? -Speed(0x2C0) : Speed(0x2C0);

			if (++npc->ani_wait > 5)
			{
				npc->ani_wait = 0;
				npc->ani_no = (npc->ani_no + 1) % 4;
			}
			SetFrame(npc, FRAME_WALK1 + npc->ani_no);

			if ((dx < PX(40) && dx > -PX(40)) || ++npc->act_wait > 90)
			{
				npc->xm = 0;
				npc->act_no = dx < PX(56) && dx > -PX(56) ? ACT_WINDUP : ACT_THINK;
				npc->act_wait = 0;
			}

			break;
		}

		case ACT_WINDUP:
			FacePlayer(npc);
			npc->xm = 0;
			SetFrame(npc, FRAME_WINDUP);
			if (npc->act_wait == 0)
				PlaySoundObject(108, 1);
			if (++npc->act_wait > Ticks(20))
			{
				npc->act_no = ACT_SWIPE;
				npc->act_wait = 0;
				npc->xm = npc->direct == 0 ? -0x600 : 0x600;
				PlaySoundObject(110, 1);
			}
			break;

		case ACT_SWIPE:
			SetFrame(npc, FRAME_SWIPE);
			npc->xm = npc->xm * 7 / 8;
			Strike(npc, npc->act_wait < 8);

			if (++npc->act_wait > 10)
			{
				Strike(npc, FALSE);
				npc->act_no = ACT_RECOVER;
				npc->act_wait = 0;
				npc->xm = 0;
			}
			break;

		case ACT_RECOVER:
			// An opening.
			SetFrame(npc, FRAME_STAND);
			if (++npc->act_wait > Ticks(26))
			{
				if (arena.phase == 3 && Random(0, 1))
					TeleportToPlayer(npc, FALSE);
				else
					npc->act_no = ACT_THINK;
				npc->act_wait = 0;
			}
			break;

		case ACT_VANISH:
			Strike(npc, FALSE);
			SetFrame(npc, FRAME_FADE1 + npc->act_wait / 4);
			if (npc->act_wait == 6)
			{
				Vulnerable(npc, FALSE);
				npc->damage = 0;
			}

			if (++npc->act_wait >= 12)
			{
				npc->act_no = ACT_HIDDEN;
				npc->act_wait = 0;
				npc->x = arena.destX;
				npc->y = arena.destY;
				npc->xm = npc->ym = 0;
			}
			break;

		case ACT_HIDDEN:
		{
			SetFrame(npc, FRAME_HIDDEN);
			int hide = Ticks(30);
			// Motes gather where it will appear.
			if (npc->act_wait > hide - 16 && npc->act_wait % 2 == 0)
			{
				unsigned char deg = (unsigned char)Random(0, 0xFF);
				SetNpChar(NPC_MOTE, npc->x + GetCos(deg) * 22, npc->y + GetSin(deg) * 22, -GetCos(deg) * 2, -GetSin(deg) * 2, MOTE_PURPLE | MOTE_TRAVEL, NULL, 0x100);
			}

			if (++npc->act_wait > hide)
			{
				npc->act_no = ACT_APPEAR;
				npc->act_wait = 0;
				PlaySoundObject(29, 1);
				if (arena.next == NEXT_STRIKE || arena.next == NEXT_THINK)
					FacePlayer(npc);
			}
			break;
		}

		case ACT_APPEAR:
			SetFrame(npc, FRAME_FADE3 - npc->act_wait / 3);
			if (++npc->act_wait >= 9)
			{
				npc->act_wait = 0;
				switch (arena.next)
				{
					case NEXT_STRIKE:
						Vulnerable(npc, TRUE);
						npc->damage = 2;
						npc->act_no = ACT_WINDUP;
						break;
					case NEXT_STEAL:
						Vulnerable(npc, TRUE);
						npc->damage = 2;
						npc->act_no = ACT_REACH;
						break;
					case NEXT_CRYSTALS:
						npc->act_no = ACT_CRYSTALS;
						break;
					default:
						Vulnerable(npc, TRUE);
						npc->damage = 2;
						npc->act_no = ACT_THINK;
						break;
				}
			}
			break;

		case ACT_REACH:
		{
			// Reach up and pull a block out of the platform.
			Stolen *s = &arena.platform[arena.stealing];
			npc->direct = PX(s->x * 16) < npc->x ? 0 : 2;
			SetFrame(npc, FRAME_REACH);

			if (++npc->act_wait == 10)
			{
				// Leave the backdrop behind it (the cell above's), not a hole to the sky.
				unsigned char behind = s->y > 0 ? gMap.data[s->x + (s->y - 1) * gMap.width] : 0;
				ChangeMapParts(s->x, s->y, GetAttribute(s->x, s->y - 1) < 0x20 ? behind : 0);
				s->wait = arena.phase == 3 ? 300 : 420;
				SetMoteBurst(PX(s->x * 16), PX(s->y * 16), 6, 0x300, MOTE_PURPLE);
				PlaySoundObject(12, 1);
			}

			if (npc->act_wait > 16)
			{
				npc->act_no = ACT_CARRY;
				npc->act_wait = 0;
				arena.throws = arena.phase == 3 ? 3 : 1;
			}
			break;
		}

		case ACT_CARRY:
			FacePlayer(npc);
			SetFrame(npc, FRAME_CARRY);
			if (++npc->act_wait > Ticks(22))
			{
				ThrowBlock(npc);
				npc->act_no = ACT_THROW;
				npc->act_wait = 0;
			}
			break;

		case ACT_THROW:
			SetFrame(npc, FRAME_THROW);
			if (++npc->act_wait > 12)
			{
				npc->act_wait = 0;
				if (--arena.throws > 0)
				{
					// Another one, out of thin air.
					SetMoteBurst(npc->x, npc->y - PX(30), 6, 0x300, MOTE_PURPLE);
					npc->act_no = ACT_CARRY;
				}
				else
				{
					npc->act_no = ACT_THINK;
				}
			}
			break;

		case ACT_PROVOKED:
			// Stared at for too long: a scream, then it's behind you.
			npc->xm = 0;
			Strike(npc, FALSE);
			SetFrame(npc, FRAME_SCREAM);
			Tremble(npc);
			if (npc->act_wait == 0)
			{
				SetQuake(20);
				PlaySoundObject(26, 1);
				PlaySoundObject(52, 1);
			}
			if (++npc->act_wait > Ticks(36))
				TeleportToPlayer(npc, TRUE);
			break;

		case ACT_CRYSTALS:
			// The second phase: crystals grow and heal it while it guards them.
			SetFrame(npc, FRAME_SCREAM);
			Tremble(npc);
			if (npc->act_wait == 0)
			{
				arena.phase = 2;
				Vulnerable(npc, TRUE);
				npc->damage = 2;
				SetQuake(40);
				PlaySoundObject(26, 1);
				GrowCrystals();
				StartTextScript(EVENT_CRYSTALS);
			}
			if (++npc->act_wait > 50)
			{
				npc->act_no = ACT_GUARD;
				npc->act_wait = 0;
				npc->ani_no = 0;
			}
			break;

		case ACT_GUARD:
		{
			FacePlayer(npc);
			int beat = ++npc->act_wait % 90;

			// Each crystal heals it a little; shoot them down first.
			int crystals = 0;
			for (int i = 0; i < arena.crystalCount; i++)
				if (gBoss[PART_CRYSTAL + i].cond & 0x80)
					++crystals;
			if (npc->act_wait % 20 == 0)
			{
				npc->life += crystals;
				if (npc->life > kPhase2Life)
					npc->life = kPhase2Life;
			}

			if (beat < 40)
			{
				SetFrame(npc, FRAME_STAND);
			}
			else if (npc->count2 % 2 == 0)
			{
				// A block out of thin air, thrown from up here.
				SetFrame(npc, beat < 76 ? FRAME_CARRY : FRAME_THROW);
				if (beat == 40)
					SetMoteBurst(npc->x, npc->y - PX(30), 8, 0x300, MOTE_PURPLE);
				if (beat == 76)
					ThrowBlock(npc);
			}
			else
			{
				// Or endermites.
				SetFrame(npc, FRAME_STARE);
				if (beat == 60 && Endermites() < 3)
				{
					for (int i = 0; i < 2; i++)
					{
						int x = arena.left + PX(Random(24, (arena.right - arena.left) / 0x200 - 24));
						SetNpChar(NPC_ENDERMITE, x, arena.floor - PX(8), 0, 0, 0, NULL, 0x40);
						SetMoteBurst(x, arena.floor - PX(8), 8, 0x400, MOTE_PURPLE);
					}
					PlaySoundObject(29, 1);
				}
			}

			if (beat == 89)
				++npc->count2;

			if (ActCrystals(npc) == 0)
			{
				// Knocked off its perch.
				Vulnerable(npc, TRUE);
				arena.stunned = TRUE;
				npc->act_no = ACT_FALL;
				npc->act_wait = 0;
				npc->ym = -0x300;
				npc->bits |= npc_ignoreSolid;
				PlaySoundObject(52, 1);
			}
			break;
		}

		case ACT_FALL:
			SetFrame(npc, FRAME_HURT);
			npc->ym += 0x40;
			if (npc->ym > 0x5FF)
				npc->ym = 0x5FF;
			npc->y += npc->ym;
			if (npc->y >= arena.floor - PX(24))
			{
				npc->y = arena.floor - PX(24);
				npc->ym = 0;
				npc->bits &= ~npc_ignoreSolid;
				npc->act_no = ACT_STUNNED;
				npc->act_wait = 0;
				SetQuake(20);
				PlaySoundObject(26, 1);
			}
			break;

		case ACT_STUNNED:
			// Down for a moment: the best time to shoot.
			SetFrame(npc, FRAME_STUN);
			if (npc->act_wait % 10 == 0)
			{
				unsigned char deg = (unsigned char)(npc->act_wait * 8);
				SetNpChar(NPC_MOTE, npc->x + GetCos(deg) * 10, npc->y - PX(30), 0, -0x80, MOTE_WHITE, NULL, 0x100);
			}
			if (++npc->act_wait > 170)
			{
				npc->act_no = ACT_THINK;
				npc->act_wait = 0;
			}
			break;

		case ACT_RAGE:
			// The last phase.
			npc->xm = 0;
			SetFrame(npc, FRAME_SCREAM);
			Tremble(npc);
			if (npc->act_wait == 0)
			{
				SetQuake(60);
				SetFlash(npc->x, npc->y, 2);
				PlaySoundObject(26, 1);
				PlaySoundObject(116, 1);
				SetMoteBurst(npc->x, npc->y - PX(12), 30, 0x900, MOTE_PURPLE);
				StartTextScript(EVENT_RAGE);
			}
			if (++npc->act_wait > 60)
			{
				npc->act_no = ACT_THINK;
				npc->act_wait = 0;
			}
			break;

		case ACT_SHADOWS:
			FacePlayer(npc);
			SetFrame(npc, FRAME_STARE);
			Tremble(npc);
			if (npc->act_wait == 0)
				SendShadows();
			if (++npc->act_wait > 40)
			{
				npc->act_no = ACT_THINK;
				npc->act_wait = 0;
			}
			break;

		case ACT_DIE:
			Die(npc);
			// Fallthrough
		case ACT_DYING:
			SetFrame(npc, npc->act_wait / 3 % 2 ? FRAME_HURT : FRAME_FALL);
			Tremble(npc);

			if (npc->act_wait % 6 == 0)
			{
				PlaySoundObject(44, 1);
				SetDestroyNpChar(npc->x + PX(Random(-10, 10)), npc->y + PX(Random(-20, 20)), PX(4), 2);
				SetMoteBurst(npc->x, npc->y, 4, 0x600, Random(0, 1) ? MOTE_PURPLE : MOTE_PINK);
			}

			if (++npc->act_wait > 150)
			{
				SetFlash(npc->x, npc->y, 1);
				SetDestroyNpChar(npc->x, npc->y, PX(16), 20);
				SetMoteBurst(npc->x, npc->y, 40, 0xA00, MOTE_PURPLE);
				PlaySoundObject(35, 1);
				PlaySoundObject(72, 1);
				DropPearl(npc);
				npc->cond = 0;
			}
			break;
	}

	// Being stared at makes it angry, whatever it's doing.
	BOOL calm = npc->act_no == ACT_THINK || npc->act_no == ACT_STALK || npc->act_no == ACT_RECOVER;
	if (npc->act_no >= ACT_FIGHT && npc->act_no < ACT_CRYSTALS)
	{
		if (arena.stareCooldown > 0)
			--arena.stareCooldown;

		if (StaredAt(npc) && npc->act_no != ACT_HIDDEN)
			++arena.stare;
		else if (arena.stare > 0)
			--arena.stare;

		if (calm && arena.stare > 40)
			SetFrame(npc, FRAME_STARE);

		if (calm && arena.stare > 80 && arena.stareCooldown == 0)
		{
			arena.stare = 0;
			arena.stareCooldown = 400;
			npc->act_no = ACT_PROVOKED;
			npc->act_wait = 0;
		}
	}

	if (npc->act_no >= ACT_FIGHT && npc->act_no != ACT_FALL && npc->act_no < ACT_DIE)
	{
		// Walk, and fall off ledges.
		npc->ym += 0x40;
		if (npc->ym > 0x5FF)
			npc->ym = 0x5FF;
		if (npc->act_no == ACT_HIDDEN || npc->act_no == ACT_GUARD || npc->act_no == ACT_CRYSTALS)
			npc->ym = 0;
		npc->x += npc->xm;
		npc->y += npc->ym;
		if (npc->flag & 8 && npc->ym > 0)
			npc->ym = 0;
		Clamp(npc);
	}

	if (npc->cond & 0x80)
	{
		if (arena.phase == 2 && npc->act_no != ACT_GUARD)
			ActCrystals(npc);
		ActBlocks();
		ActShadows();
		RestorePlatforms(FALSE);
	}
}

int FlintBossPhase(void)
{
	return arena.phase;
}

void FlintBossSetPhase(int phase)
{
	arena.phase = phase;
	arena.stunned = phase >= 3;
}
