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
// crystals grow, 1020 for the last phase's cutscene, and 1000 when it dies;
// event 1000 should <BOA0500 for the death. The pearl it drops runs event
// 1100. Event 1020 steps the cutscene on with <BOA0320 (it looks out of the
// screen), <BOA0330 (it transforms) and <BOA0340 (it breaks out); without a
// script it goes on by itself.
#include "BossFlint.h"

#include <math.h>
#include <string.h>

#include "WindowsWrapper.h"

#include "Boss.h"
#include "Caret.h"
#include "Draw.h"
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
#include "ValueView.h"
#include "Web.h"

extern TEXT_SCRIPT gTS; // TextScr.cpp: its mode is 0 when no script is running

#define PX(n) ((n) * 0x200)

static const int kLife = 300;
static const int kPhase2Life = 200; // crystals
static const int kPhase3Life = 100; // rage
// The last phase starts back at full life; it tears the site apart further at
// each of these (crumble 1-3).
static const int kCrumbleLife[3] = {225, 150, 75};

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

// What it throws in off the site: a piece of the page (falls), a visitor's
// cursor (a stray cursor in the room) or a webring button (flies across).
enum
{
	INCOMING_PIECE,
	INCOMING_CURSOR,
	INCOMING_BUTTON
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
	SHADOWS = 2,
	PART_HAND = 11 // the last phase: its hand, reaching in from outside
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
	ACT_RAGE = 300, // the last phase's cutscene: it gets up
	ACT_LEAVE = 301, // out through the top of the screen
	ACT_OUTSIDE = 302, // on fl1nt.dev, throwing pieces of it in
	ACT_DROP = 303, // back down into the room
	ACT_SHADOWS = 310,
	// The cutscene's acts, which <BOA starts: each sets itself up, then runs
	// as the next number.
	ACT_LOOK = 320, // it looks out of the screen at you
	ACT_TRANSFORM = 330, // into its last form, its life coming back
	ACT_TRANSFORMED = 335,
	ACT_BREAK = 340, // it screams until the screen breaks, and climbs out
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

	// The last phase, when it breaks out of the screen onto fl1nt.dev (the
	// page draws that side of it; see FlintEmit).
	int top; // the ceiling's underside, fixed point
	BOOL outside; // out of the room, on the site
	int round; // attacks left before it drops back in
	int insideWait, insideLife; // since it came back in, and its life then
	int crumble; // 0-3: how far it's torn things apart
	int lastLife;
	int reach, reachWait; // its hand: see ActHand
	int tilt, tiltWait; // the screen tipped one way (-1, 1)
	int debris, debrisCount;
	// Things off the site on their way in (INCOMING_*): where and when each
	// lands, and what it is. picture: the page's picture of it, once it's
	// drawn one (its SURFACE_ID_FLINT_SITE slot + 1, w by h).
	struct { int wait, x, y, kind, what, count; BOOL told; int picture, w, h; } incoming[8];
	int stack[3]; // the last stack of buttons' incoming, in order
	// Pictures the page has ready of pieces of the site, for the ones that
	// fall from the ceiling (slot + 1, w, h; 0 once one's fallen).
	struct { int picture, w, h; } pool[6];
	BOOL rage; // in its last form, drawn from SURFACE_ID_FLINT_RAGE
	int idle; // cutscene frames without a script to move it on
	int handLife; // its life when its hand came in
} arena;

static BOOL Solid(unsigned char attribute)
{
	return attribute == 0x41 || attribute == 0x05 || attribute == 0x43;
}

static int TileOf(int v)
{
	return (v / 0x200 + 8) / 16;
}

// Its last form is half as big again, on a sheet of its own (Npc/NpcFlintRage,
// 48x72 frames in the same order). Its feet stay where they were.
static void SetFrame(NPCHAR *npc, int frame)
{
	if (frame == FRAME_HIDDEN)
	{
		npc->rect.left = npc->rect.right = npc->rect.top = npc->rect.bottom = 0;
		return;
	}

	if (arena.rage)
	{
		npc->surf = SURFACE_ID_FLINT_RAGE;
		npc->rect.left = (frame % 10) * 48;
		npc->rect.top = (frame / 10) * 144 + (npc->direct == 0 ? 0 : 72);
		npc->rect.right = npc->rect.left + 48;
		npc->rect.bottom = npc->rect.top + 72;
		npc->view.front = npc->view.back = PX(24);
		npc->view.top = PX(48);
		npc->view.bottom = PX(24);
		return;
	}

	npc->surf = (Surface_Ids)0; // the stage's sheet
	npc->rect.left = (frame % 10) * 32;
	npc->rect.top = (frame / 10) * 96 + (npc->direct == 0 ? 0 : 48);
	npc->rect.right = npc->rect.left + 32;
	npc->rect.bottom = npc->rect.top + 48;
	npc->view.front = npc->view.back = PX(16);
	npc->view.top = npc->view.bottom = PX(24);
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
	arena.top = PX(ceilingRow * 16 + 8);

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

// ------------------------------------------------------------------ the last phase

static void Strike(NPCHAR *npc, BOOL on);

static int ScreenX(int x)
{
	return (x - gFrame.x) / 0x200;
}

static int ScreenY(int y)
{
	return (y - gFrame.y) / 0x200;
}

static int InRoom(int x, int margin)
{
	if (x < arena.left + margin)
		return arena.left + margin;
	if (x > arena.right - margin)
		return arena.right - margin;
	return x;
}

// The cutscene's acts wait for event 1020's next <BOA. With no script running
// (a level without the event), each moves on by itself after a while.
static void Unscripted(NPCHAR *npc, int next, int wait)
{
	if (gTS.mode != 0)
	{
		arena.idle = 0;
		return;
	}
	if (++arena.idle > wait)
	{
		arena.idle = 0;
		npc->act_no = next;
		npc->act_wait = 0;
	}
}

// Out of the room the way it came: up through the top of the screen, in
// the middle (it's on top of the window there, on the page).
static void Leave(NPCHAR *npc)
{
	Vulnerable(npc, FALSE);
	Strike(npc, FALSE);
	npc->damage = 0;
	npc->ym = -0x900;
	npc->xm = ((arena.left + arena.right) / 2 - npc->x) / ((npc->y - (arena.top - PX(48))) / 0x900 + 1);
	npc->bits |= npc_ignoreSolid; // through platforms and ceiling alike
	npc->act_no = ACT_LEAVE;
	npc->act_wait = 0;
	PlaySoundObject(29, 1);
	SetMoteBurst(npc->x, npc->y, 16, 0x600, MOTE_PURPLE);
}

static void GoOutside(NPCHAR *npc)
{
	arena.outside = TRUE;
	arena.round = 4 + arena.crumble / 2;
	npc->act_no = ACT_OUTSIDE;
	// A moment before it starts: the page is still opening the site up (and
	// the first time, out of the cutscene, it types its words into the site).
	npc->act_wait = g_GameFlags & 2 ? -60 : -120;
	npc->xm = npc->ym = 0;
	npc->x = (arena.left + arena.right) / 2;
	npc->y = arena.top - PX(64);
	SetFrame(npc, FRAME_HIDDEN);
	FlintEmit("out", ScreenX(npc->x), 0);
}

// Something it takes off the site, on its way in: it arrives in the room
// `wait` frames from now. The page is told (FlintEmit) when it's kLead
// frames from arriving: long enough to show it plainly being taken off the
// page, held up, and thrown. Told already: it just falls in, unannounced.
// Either way the page is told again kThrow frames before it lands, as it's
// thrown, so the page's throw lands with it however fast the game is
// running; and for the last kWarn frames motes trickle in where it'll land.
static const int kLead[3] = {108, 108, 108}; // INCOMING_*
static const int kThrow = 23;
static const int kWarn = 40;

static int Incoming(int what, int x, int y, int kind, int count, int later, BOOL told)
{
	for (int i = 0; i < 8; i++)
	{
		if (arena.incoming[i].wait > 0)
			continue;
		arena.incoming[i].wait = kLead[what] + later;
		arena.incoming[i].what = what;
		arena.incoming[i].x = x;
		arena.incoming[i].y = y;
		arena.incoming[i].kind = kind;
		arena.incoming[i].count = count;
		arena.incoming[i].told = told;
		arena.incoming[i].picture = 0;
		return i;
	}
	return -1;
}

// It tears a piece off the site and throws it in through the top of the
// screen, where it breaks into `count` pieces that fall on you.
static void ThrowPiece(int x, int count)
{
	Incoming(INCOMING_PIECE, InRoom(x, PX(20)), 0, Random(0, PIECE_KINDS - 1), count, 0, FALSE);
}

// A piece of the site that just falls in through the top (with its hand or
// the tipping, so the page has its hands full). The page is told, so it can
// show it coming loose from the site and draw it.
static void DropPiece(int x)
{
	const int kind = Random(0, PIECE_KINDS - 1);
	const int i = Incoming(INCOMING_PIECE, InRoom(x, PX(20)), 0, kind, 1, 20, TRUE);
	if (i >= 0)
		FlintEmit("fall", kind | i << 8, ScreenX(arena.incoming[i].x));
}

// Stray cursors in the room, from the site or the level.
static int Cursors(void)
{
	int count = 0;
	for (int i = 0; i < NPC_MAX; i++)
		if ((gNPC[i].cond & 0x80) && gNPC[i].code_char == NPC_STRAY_CURSOR && gNPC[i].x > arena.left && gNPC[i].x < arena.right && gNPC[i].y < arena.floor)
			++count;
	return count;
}

// It snatches a handful of visitors' cursors off the site and throws them
// in: stray cursors, in their colour, that hunt you.
static void ThrowCursors(int x, int count)
{
	Incoming(INCOMING_CURSOR, InRoom(x, PX(24 + 20 * count)), arena.top + PX(28), Random(0, 2), count, 0, FALSE);
}

// It grabs a stack of the site's webring buttons and flings them in through
// the side of the window one after another, each low (jump it) or high
// (stay down) by `high`'s bits. dir is where they fly (0 left, 2 right).
static void ThrowButtons(int dir, int count, int high)
{
	const int x = dir == 2 ? arena.left - PX(8) : arena.right + PX(8);
	if (count > 3)
		count = 3;
	for (int i = 0; i < count; i++)
		arena.stack[i] = Incoming(INCOMING_BUTTON, x, arena.floor - PX(high & (1 << i) ? 34 : 8), dir, high, i * 20, i > 0);
}

// SetNpChar, and the NPC it made (NULL if the room's full).
static NPCHAR *Spawn(int code, int x, int y, int xm, int ym, int dir)
{
	int n = 0x100;
	while (n < NPC_MAX && gNPC[n].cond)
		++n;
	if (n == NPC_MAX)
		return NULL;
	SetNpChar(code, x, y, xm, ym, dir, NULL, n);
	return &gNPC[n];
}

// A piece of the site comes down from the ceiling at x: one the page has a
// picture of ready, if there is one (it's told which, so it can show that
// coming off the site and draw another), or else just its kind.
static void Debris(int x)
{
	int ready[6];
	int n = 0;
	for (int p = 0; p < 6; p++)
		if (arena.pool[p].picture)
			ready[n++] = p;
	NPCHAR *piece = Spawn(NPC_PAGE_PIECE, x, arena.top + PX(10), 0, 0x100, Random(0, PIECE_KINDS - 1));
	if (piece == NULL || n == 0)
		return;
	const int p = ready[Random(0, n - 1)];
	SetSitePicture(piece, arena.pool[p].picture - 1, 0, arena.pool[p].w, arena.pool[p].h);
	arena.pool[p].picture = 0;
	FlintEmit("debris", p, ScreenX(x));
}

// Motes where something from the site is about to land: down from the
// ceiling over each piece or cursor, or in from the wall a button comes
// through, at its height.
static void Warn(int i)
{
	const int x = arena.incoming[i].x;
	const int count = arena.incoming[i].count;
	switch (arena.incoming[i].what)
	{
		case INCOMING_PIECE:
			for (int k = 0; k < count; k++)
				SetNpChar(NPC_MOTE, InRoom(x + PX((k - (count - 1) / 2) * 36 + Random(-6, 6)), PX(20)), arena.top + PX(4), 0, 0x280, MOTE_WHITE | MOTE_TRAVEL, NULL, 0x100);
			break;
		case INCOMING_CURSOR:
			for (int k = 0; k < count; k++)
				SetNpChar(NPC_MOTE, InRoom(x + PX((k * 2 - (count - 1)) * 20 + Random(-4, 4)), PX(24)), arena.top + PX(4), 0, 0x280, MOTE_CYAN | MOTE_TRAVEL, NULL, 0x100);
			break;
		case INCOMING_BUTTON:
		{
			const BOOL fromLeft = arena.incoming[i].kind == 2;
			SetNpChar(NPC_MOTE, fromLeft ? arena.left + PX(2) : arena.right - PX(2), arena.incoming[i].y + PX(Random(-3, 3)), fromLeft ? 0x280 : -0x280, 0, MOTE_WHITE | MOTE_TRAVEL, NULL, 0x100);
			break;
		}
	}
}

static void ActIncoming(void)
{
	for (int i = 0; i < 8; i++)
	{
		if (arena.incoming[i].wait <= 0)
			continue;
		const int what = arena.incoming[i].what;
		const int x = arena.incoming[i].x;
		if (!arena.incoming[i].told && arena.incoming[i].wait <= kLead[what])
		{
			// For a stack of buttons, the page is told about them all at once:
			// how many, and which are high (count holds those bits).
			arena.incoming[i].told = TRUE;
			if (what == INCOMING_PIECE)
				FlintEmit("throw", arena.incoming[i].kind | arena.incoming[i].count << 4 | i << 8, ScreenX(x));
			else if (what == INCOMING_CURSOR)
				FlintEmit("cursor", arena.incoming[i].kind | arena.incoming[i].count << 4, ScreenX(x));
			else
			{
				int stack = 1;
				for (int k = 0; k < 8; k++)
					if (k != i && arena.incoming[k].wait > arena.incoming[i].wait && arena.incoming[k].what == INCOMING_BUTTON)
						++stack;
				FlintEmit("button", arena.incoming[i].kind | stack << 4 | arena.incoming[i].count << 8, ScreenY(arena.floor - PX(8)));
			}
		}
		if (arena.incoming[i].wait == kThrow)
			FlintEmit("thrown", what, 0);
		if (arena.incoming[i].wait <= kWarn && arena.incoming[i].wait % 5 == 0)
			Warn(i);
		if (--arena.incoming[i].wait > 0)
			continue;
		switch (what)
		{
			case INCOMING_PIECE:
				// It breaks apart coming in: its pieces fall spread out, each a
				// slice of the page's picture of it.
				for (int k = 0; k < arena.incoming[i].count; k++)
				{
					const int count = arena.incoming[i].count;
					const int px = InRoom(x + PX((k - (count - 1) / 2) * 36 + Random(-6, 6)), PX(20));
					const int xm = (gMC.x > px ? 1 : -1) * Random(0x80, 0x240);
					NPCHAR *piece = Spawn(NPC_PAGE_PIECE, px, arena.top + PX(10 + k * 6), xm, 0x100, (arena.incoming[i].kind + k) % PIECE_KINDS);
					const int w = arena.incoming[i].w;
					if (piece != NULL && arena.incoming[i].picture)
						SetSitePicture(piece, arena.incoming[i].picture - 1, k * w / count, (k + 1) * w / count - k * w / count, arena.incoming[i].h);
				}
				SetMoteBurst(x, arena.top + PX(6), 6 + arena.incoming[i].count * 3, 0x400, MOTE_WHITE);
				PlaySoundObject(39, 1);
				break;
			case INCOMING_CURSOR:
				for (int k = 0; k < arena.incoming[i].count; k++)
				{
					const int cx = InRoom(x + PX((k * 2 - (arena.incoming[i].count - 1)) * 20), PX(24));
					SetNpChar(NPC_STRAY_CURSOR, cx, arena.incoming[i].y + PX(k % 2 * 10), 0, 0, 10 + arena.incoming[i].kind, NULL, 0x100);
					SetMoteBurst(cx, arena.top + PX(6), 5, 0x400, MOTE_CYAN);
				}
				PlaySoundObject(39, 1);
				break;
			case INCOMING_BUTTON:
			{
				NPCHAR *button = Spawn(NPC_WEB_BUTTON, x, arena.incoming[i].y, 0, 0, arena.incoming[i].kind);
				if (button != NULL && arena.incoming[i].picture)
					SetSitePicture(button, arena.incoming[i].picture - 1, 0, arena.incoming[i].w, arena.incoming[i].h);
				PlaySoundObject(39, 1);
				break;
			}
		}
	}
}

// Its hand, reached in from outside: it hangs over you, slams down, sits there
// (shoot it: that's the enderman you're hurting), drags across the floor at
// you, and pulls back out. reach: 1 aiming, 2 slamming, 3 planted, 4 dragged,
// 5 pulled out.
static void StartReach(int x, int aim)
{
	NPCHAR *hand = &gBoss[PART_HAND];
	memset(hand, 0, sizeof(*hand));
	hand->cond = 0x80 | 0x10; // hits go to the enderman
	hand->x = InRoom(x, PX(16));
	hand->y = arena.top - PX(12);
	hand->bits = npc_ignoreSolid | npc_shootable;
	hand->hit_voice = 52;
	hand->hit.front = hand->hit.back = PX(11);
	hand->hit.top = PX(10);
	hand->hit.bottom = PX(13);
	hand->view.front = hand->view.back = PX(16);
	hand->view.top = hand->view.bottom = PX(16);
	arena.reach = 1;
	arena.reachWait = 36 - aim; // aiming time left
	arena.handLife = gBoss[0].life;
	FlintEmit("reach", ScreenX(hand->x), 0);
	PlaySoundObject(103, 1);
}

static void ActHand(void)
{
	NPCHAR *hand = &gBoss[PART_HAND];
	if (!arena.reach)
		return;

	// Hurt enough through it, it snatches the hand back out.
	if (arena.reach < 5 && arena.handLife - gBoss[0].life >= 24)
	{
		arena.reach = 5;
		hand->damage = 0;
		SetQuake(10);
		PlaySoundObject(52, 1);
		SetMoteBurst(hand->x, hand->y, 12, 0x500, MOTE_PURPLE);
	}

	switch (arena.reach)
	{
		case 1:
			// Following you along the ceiling; motes fall where it'll land.
			hand->x += hand->x < gMC.x ? 0x120 : -0x120;
			hand->x = InRoom(hand->x, PX(16));
			if (arena.reachWait % 4 == 0)
				SetNpChar(NPC_MOTE, hand->x + PX(Random(-8, 8)), arena.top + PX(6), 0, 0x280, MOTE_PURPLE | MOTE_TRAVEL, NULL, 0x100);
			if (--arena.reachWait <= 0)
			{
				arena.reach = 2;
				hand->ym = 0x400;
				hand->damage = arena.crumble >= 2 ? 5 : 4;
				PlaySoundObject(110, 1);
				FlintEmit("slam", ScreenX(hand->x), 0);
			}
			break;

		case 2:
			hand->ym += 0x100;
			if (hand->ym > 0xC00)
				hand->ym = 0xC00;
			hand->y += hand->ym;
			if (hand->y >= arena.floor - PX(14))
			{
				hand->y = arena.floor - PX(14);
				hand->damage = 2;
				arena.reach = 3;
				arena.reachWait = 0;
				SetQuake(30);
				PlaySoundObject(26, 1);
				PlaySoundObject(44, 1);
				SetMoteBurst(hand->x, arena.floor, 14, 0x600, MOTE_PURPLE);
				Shatter(hand->x - PX(12), arena.floor - PX(4));
				Shatter(hand->x + PX(12), arena.floor - PX(4));
			}
			break;

		case 3:
			if (++arena.reachWait > 60 - arena.crumble * 8)
			{
				arena.reach = 4;
				arena.reachWait = 0;
				hand->direct = gMC.x < hand->x ? 0 : 2;
				hand->damage = 3;
				PlaySoundObject(110, 1);
			}
			break;

		case 4:
			hand->x += hand->direct == 0 ? -0x3A0 : 0x3A0;
			if (++arena.reachWait % 4 == 0)
				SetNpChar(NPC_BLOCK_SHARD, hand->x, arena.floor - PX(2), Random(-0x200, 0x200), -0x300, SHARD_END_STONE, NULL, 0x100);
			if (hand->x <= arena.left + PX(20) || hand->x >= arena.right - PX(20) || arena.reachWait > 64)
			{
				arena.reach = 5;
				hand->damage = 0;
			}
			break;

		case 5:
			hand->y -= 0x900;
			if (hand->y < arena.top - PX(28))
			{
				hand->cond = 0;
				arena.reach = 0;
				FlintEmit("unreach", 0, 0);
			}
			break;
	}

	// Its sprite is drawn with the arm (PutFlintExtras).
	hand->rect.left = hand->rect.right = 0;
}

// It grabs the screen and tips it: you slide.
static void StartTilt(int dir)
{
	arena.tilt = dir;
	arena.tiltWait = 110;
	SetQuake(110);
	PlaySoundObject(26, 1);
	FlintEmit("tilt", dir, 0);
}

static void ActTilt(void)
{
	if (arena.tiltWait <= 0)
		return;

	if (--arena.tiltWait > 12)
		gMC.xm += arena.tilt * (0x2C + arena.crumble * 0x0A);
	if (arena.tiltWait == 0)
		FlintEmit("untilt", 0, 0);
}

static void ActOutside(NPCHAR *npc)
{
	SetFrame(npc, FRAME_HIDDEN);
	ActIncoming();
	ActHand();
	ActTilt();

	// Where its hand is, so the damage you do shows there.
	NPCHAR *hand = &gBoss[PART_HAND];
	if (hand->cond & 0x80)
	{
		npc->x = hand->x;
		npc->y = hand->y;
	}

	if (arena.stareCooldown > 0)
		--arena.stareCooldown;

	// One thing at a time: its hand, the tipping, or what it's thrown still
	// on its way in.
	if (arena.reach || arena.tiltWait > 0)
		return;
	for (int i = 0; i < 8; i++)
		if (arena.incoming[i].wait > 0)
			return;

	// Nothing new while the cutscene still has you.
	if (!(g_GameFlags & 2))
		return;

	if (++npc->act_wait < 50 - arena.crumble * 8)
		return;
	npc->act_wait = 0;

	if (arena.round-- <= 0)
	{
		// Back into the room, from above.
		arena.outside = FALSE;
		int x = gMC.x < (arena.left + arena.right) / 2 ? arena.right - PX(Random(30, 70)) : arena.left + PX(Random(30, 70));
		npc->x = InRoom(x, PX(16));
		npc->y = arena.top - PX(24);
		npc->ym = 0x200;
		npc->bits |= npc_ignoreSolid;
		npc->act_no = ACT_DROP;
		npc->act_wait = 0;
		FacePlayer(npc);
		PlaySoundObject(29, 1);
		FlintEmit("in", ScreenX(npc->x), 0);
		return;
	}

	// A volley of pieces, its hand, the room tipped over, visitors' cursors
	// set on you, or webring buttons flung through the room; later on, the
	// hand and the tipping come with pieces too, and there's more of the rest.
	const int more = arena.crumble >= 2 ? 1 : 0;
	int roll = Random(0, 99);
	if (roll >= 72 && roll < 86 && Cursors() + 2 + more > 4)
		roll = 86; // enough cursors already
	if (roll < 30)
	{
		ThrowPiece(gMC.x + PX(Random(-40, 40)), 2 + more);
	}
	else if (roll < 62)
	{
		StartReach(gMC.x, arena.crumble * 6);
		if (arena.crumble >= 1)
			DropPiece(gMC.x + PX(Random(-64, 64)));
	}
	else if (roll < 72)
	{
		StartTilt(gMC.x < (arena.left + arena.right) / 2 ? 1 : -1);
		if (arena.crumble >= 1)
			DropPiece(gMC.x + PX(Random(-64, 64)));
	}
	else if (roll < 86)
	{
		ThrowCursors(arena.left + PX(Random(60, (arena.right - arena.left) / 0x200 - 60)), 2 + more);
	}
	else
	{
		// A stack of them from one side, low and high mixed so you have to
		// read them (never all high).
		const int count = 2 + more;
		int high = Random(0, (1 << count) - 1);
		if (high == (1 << count) - 1)
			high = 0;
		ThrowButtons(Random(0, 1) ? 0 : 2, count, high);
	}
}

static BOOL InCutscene(NPCHAR *npc)
{
	return npc->act_no == ACT_RAGE || (npc->act_no >= ACT_LOOK && npc->act_no <= ACT_BREAK + 1);
}

static void ActLastPhase(NPCHAR *npc)
{
	// Nothing comes apart during the cutscene.
	if (InCutscene(npc) || !(g_GameFlags & 2))
	{
		arena.lastLife = npc->life;
		return;
	}

	// How far it's torn things apart, by its life.
	int crumble = npc->life <= kCrumbleLife[2] ? 3 : npc->life <= kCrumbleLife[1] ? 2 : npc->life <= kCrumbleLife[0] ? 1 : 0;
	if (crumble > arena.crumble && npc->act_no < ACT_DIE)
	{
		arena.crumble = crumble;
		SetQuake(60);
		PlaySoundObject(26, 1);
		FlintEmit("crumble", crumble, 0);
	}

	if (npc->life < arena.lastLife)
		FlintEmit("hurt", npc->life, arena.outside);
	arena.lastLife = npc->life;

	// The room is coming apart too: bits of its ceiling fall, and later on
	// pieces of the site come down with them.
	if (npc->act_no < ACT_DIE && ++arena.debris > 36 - arena.crumble * 8)
	{
		arena.debris = 0;
		int x = arena.left + PX(Random(8, (arena.right - arena.left) / 0x200 - 8));
		if (arena.crumble >= 2 && ++arena.debrisCount % 4 == 0)
			Debris(x);
		else
			SetNpChar(NPC_BLOCK_SHARD, x, arena.top + PX(4), Random(-0x80, 0x80), 0, Random(0, 1), NULL, 0x100);
	}

	if (!arena.outside && npc->act_no >= ACT_FIGHT && npc->act_no < ACT_CRYSTALS)
		++arena.insideWait;
}

// Everything it has going in the room goes: its hand and anything on the
// way in, blocks, shadows, crystals and endermites. The platforms come back.
static void ClearRoom(void)
{
	gBoss[PART_HAND].cond = 0;
	arena.reach = 0;
	arena.tiltWait = 0;
	for (int i = 0; i < 8; i++)
		arena.incoming[i].wait = 0;

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

	for (int i = 0; i < NPC_MAX; i++)
	{
		if ((gNPC[i].cond & 0x80) && gNPC[i].code_char == NPC_ENDERMITE && gNPC[i].x > arena.left && gNPC[i].x < arena.right && gNPC[i].y < arena.floor)
		{
			SetMoteBurst(gNPC[i].x, gNPC[i].y, 6, 0x400, MOTE_PURPLE);
			gNPC[i].cond = 0;
		}
	}

	RestorePlatforms(TRUE);
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
		// The last phase opens with a cutscene (event 1020): the room is
		// cleared and it can't be hurt until it's out of the screen.
		arena.phase = 3;
		ClearRoom();
		Vulnerable(npc, FALSE);
		npc->damage = 0;
		Strike(npc, FALSE);
		npc->act_no = ACT_RAGE;
		npc->act_wait = 0;
		arena.idle = 0;
		StartTextScript(EVENT_RAGE);
		FlintEmit("rage", ScreenX(npc->x), ScreenY(npc->y - PX(10)));
		return;
	}

	// In the last phase it only stays in the room a while.
	if (arena.phase == 3 && (arena.insideWait > 400 || arena.insideLife - npc->life >= 45))
	{
		Leave(npc);
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
	// reach runs from above its head down to just over the floor. Its last
	// form reaches further.
	const int reach = arena.rage ? PX(28) : PX(20);
	swipe->x = npc->x + (npc->direct == 0 ? -reach : reach);
	swipe->y = npc->y;
	swipe->damage = 3;
	swipe->bits = npc_ignoreSolid;
	swipe->hit.front = swipe->hit.back = arena.rage ? PX(18) : PX(14);
	swipe->hit.top = arena.rage ? PX(34) : PX(22);
	swipe->hit.bottom = PX(22);
}

static void Die(NPCHAR *npc)
{
	// Out on the site (or on its way), it falls back into the room to die.
	NPCHAR *hand = &gBoss[PART_HAND];
	if (arena.outside || npc->act_no == ACT_LEAVE || npc->act_no == ACT_DROP || npc->y < arena.top)
	{
		npc->x = InRoom(hand->cond & 0x80 ? hand->x : (arena.left + arena.right) / 2, PX(16));
		npc->y = arena.floor - PX(24);
		npc->direct = gMC.x < npc->x ? 0 : 2;
	}
	arena.outside = FALSE;
	npc->bits &= ~npc_ignoreSolid;
	if (arena.phase == 3)
		FlintEmit("die", ScreenX(npc->x), ScreenY(npc->y));

	Vulnerable(npc, FALSE);
	npc->damage = 0;
	npc->xm = 0;
	npc->act_no = ACT_DYING;
	npc->act_wait = 0;
	Strike(npc, FALSE);

	// Everything of its goes with it.
	ClearRoom();
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
	// Its last form's sheet, which the page writes (a level without one just
	// doesn't show that form).
	ReloadBitmap_File("Npc/NpcFlintRage", SURFACE_ID_FLINT_RAGE);
	// And the page's pictures of the site (it draws them as it goes).
	ReloadBitmap_File("Npc/NpcSite", SURFACE_ID_FLINT_SITE);
	// A fresh fight: the page puts anything broken back.
	FlintEmit("reset", 0, 0);
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
			// Like the dragon, it heals off its crystals, all the way back up
			// if you let it: a beam from each (PutFlintExtras), and the life
			// it gets back over its head.
			if (npc->act_wait % 24 == 0 && crystals > 0 && npc->life < kLife)
			{
				int heal = crystals;
				if (npc->life + heal > kLife)
					heal = kLife - npc->life;
				npc->life += heal;
				SetValueView(&npc->x, &npc->y, heal);
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
			// The last phase's cutscene: it gets up off the floor, shaking,
			// and waits for the script.
			npc->xm = 0;
			FacePlayer(npc);
			SetFrame(npc, npc->act_wait < 40 ? FRAME_STUN : FRAME_STAND);
			if (npc->act_wait >= 40)
				Tremble(npc);
			if (npc->act_wait % 24 == 12)
				SetNpChar(NPC_MOTE, npc->x + PX(Random(-8, 8)), npc->y - PX(20), 0, 0, MOTE_PURPLE, NULL, 0x100);
			++npc->act_wait;
			Unscripted(npc, ACT_LOOK, 100);
			break;

		case ACT_LOOK:
			// It looks out of the screen, at whoever is behind it.
			npc->act_no = ACT_LOOK + 1;
			npc->act_wait = 0;
			arena.idle = 0;
			SetQuake(12);
			PlaySoundObject(108, 1);
			FlintEmit("look", ScreenX(npc->x), ScreenY(npc->y - PX(10)));
			// Fallthrough
		case ACT_LOOK + 1:
			SetFrame(npc, FRAME_STARE);
			if (npc->act_wait % 16 == 0)
				SetNpChar(NPC_MOTE, npc->x + PX(Random(-10, 10)), npc->y - PX(24), 0, 0, MOTE_PINK, NULL, 0x100);
			++npc->act_wait;
			Unscripted(npc, ACT_TRANSFORM, 100);
			break;

		case ACT_TRANSFORM:
			npc->act_no = ACT_TRANSFORM + 1;
			npc->act_wait = 0;
			SetQuake(160);
			PlaySoundObject(26, 1);
			PlaySoundObject(20, 1);
			FlintEmit("transform", ScreenX(npc->x), ScreenY(npc->y - PX(10)));
			// Fallthrough
		case ACT_TRANSFORM + 1:
		{
			// It pulls what's left of /dev/null into itself and rises,
			// flickering into its last form, while its life comes back.
			const int t = npc->act_wait++;
			npc->y = arena.floor - PX(24) - PX(t < 64 ? t / 4 : 16);
			const int period = t < 50 ? 10 : t < 90 ? 6 : t < 120 ? 3 : 2;
			arena.rage = (t / period) % 2 == 1;
			SetFrame(npc, FRAME_SCREAM);
			Tremble(npc);
			if (t % 2 == 0)
			{
				// Motes from all around, falling into it.
				const unsigned char deg = (unsigned char)Random(0, 255);
				SetNpChar(NPC_MOTE, npc->x + GetCos(deg) * 52, npc->y - PX(12) + GetSin(deg) * 52, -GetCos(deg) * 52 / 29, -GetSin(deg) * 52 / 29, (Random(0, 2) ? MOTE_PURPLE : MOTE_WHITE) | MOTE_TRAVEL, NULL, 0x100);
			}
			if (t % 12 == 0)
				PlaySoundObject(t < 100 ? 103 : 110, 1);
			if (npc->life < kLife)
			{
				npc->life += 2;
				if (npc->life > kLife)
					npc->life = kLife;
			}
			if (t >= 150)
			{
				// Done: bigger, and back to full life.
				arena.rage = TRUE;
				npc->life = kLife;
				arena.lastLife = kLife;
				npc->hit.front = npc->hit.back = PX(9);
				npc->hit.top = PX(32);
				SetFlash(npc->x, npc->y, 2);
				SetQuake(40);
				PlaySoundObject(35, 1);
				PlaySoundObject(116, 1);
				SetMoteBurst(npc->x, npc->y - PX(16), 40, 0xA00, MOTE_PURPLE);
				FlintEmit("transformed", ScreenX(npc->x), ScreenY(npc->y - PX(20)));
				npc->act_no = ACT_TRANSFORMED;
			}
			break;
		}

		case ACT_TRANSFORMED:
			npc->act_no = ACT_TRANSFORMED + 1;
			npc->act_wait = 0;
			arena.idle = 0;
			// Fallthrough
		case ACT_TRANSFORMED + 1:
			// Its last form: it comes back down, screaming, then glares.
			FacePlayer(npc);
			if (npc->y < arena.floor - PX(24))
			{
				npc->y += PX(2);
				if (npc->y > arena.floor - PX(24))
					npc->y = arena.floor - PX(24);
			}
			SetFrame(npc, npc->act_wait < 50 ? FRAME_SCREAM : FRAME_STARE);
			if (npc->act_wait < 50)
				Tremble(npc);
			++npc->act_wait;
			Unscripted(npc, ACT_BREAK, 100);
			break;

		case ACT_BREAK:
			npc->act_no = ACT_BREAK + 1;
			npc->act_wait = 0;
			SetQuake(120);
			SetFlash(npc->x, npc->y, 2);
			PlaySoundObject(26, 1);
			PlaySoundObject(116, 1);
			SetMoteBurst(npc->x, npc->y - PX(12), 30, 0x900, MOTE_PURPLE);
			FlintEmit("break", ScreenX(npc->x), ScreenY(npc->y - PX(10)));
			// Fallthrough
		case ACT_BREAK + 1:
			// It screams until the screen cracks, then climbs out through the
			// top of it, onto fl1nt.dev.
			npc->xm = 0;
			npc->y = arena.floor - PX(24);
			SetFrame(npc, npc->act_wait < 96 ? FRAME_SCREAM : FRAME_REACH);
			if (npc->act_wait < 96)
				Tremble(npc);
			if (npc->act_wait == 36 || npc->act_wait == 64)
			{
				SetQuake(30);
				PlaySoundObject(26, 1);
				PlaySoundObject(44, 1);
				FlintEmit("crack", ScreenX(npc->x), ScreenY(npc->y - PX(10)));
			}
			if (++npc->act_wait > 116)
				Leave(npc);
			break;

		case ACT_LEAVE:
			SetFrame(npc, FRAME_REACH);
			npc->x += npc->xm;
			npc->y += npc->ym;
			if (npc->act_wait++ % 3 == 0)
				SetMoteBurst(npc->x, npc->y + PX(16), 3, 0x300, MOTE_PURPLE);
			if (npc->y < arena.top - PX(48))
				GoOutside(npc);
			break;

		case ACT_OUTSIDE:
			ActOutside(npc);
			break;

		case ACT_DROP:
			// Back down into the room from above.
			SetFrame(npc, FRAME_FALL);
			npc->ym += 0x40;
			if (npc->ym > 0x6FF)
				npc->ym = 0x6FF;
			npc->y += npc->ym;
			if (npc->y >= arena.floor - PX(24))
			{
				npc->y = arena.floor - PX(24);
				npc->ym = 0;
				npc->bits &= ~npc_ignoreSolid;
				SetQuake(30);
				PlaySoundObject(26, 1);
				SetMoteBurst(npc->x, arena.floor, 16, 0x500, MOTE_PURPLE);
				Vulnerable(npc, TRUE);
				npc->damage = 3;
				arena.insideWait = 0;
				arena.insideLife = npc->life;
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

	if (npc->act_no >= ACT_FIGHT && npc->act_no != ACT_FALL && npc->act_no < ACT_DIE && !(npc->act_no >= ACT_RAGE && npc->act_no <= ACT_DROP) && !InCutscene(npc))
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
		if (arena.phase == 3)
			ActLastPhase(npc);
	}
}

void FlintBossTestThrow(int what, int a, int b)
{
	const int x = gFrame.x + PX(a);
	if (what == INCOMING_PIECE)
		ThrowPiece(x, b > 0 ? b : 1);
	else if (what == INCOMING_CURSOR)
		ThrowCursors(x, b > 0 ? b : 1);
	else if (what == INCOMING_BUTTON)
		ThrowButtons(a, b >> 4 > 0 ? b >> 4 : 1, b & 15);
	else if (what == 4)
		DropPiece(x);
	else if (what == 5)
		Debris(x);
	else if (gBoss[0].act_no == ACT_OUTSIDE)
		gBoss[0].act_wait = a ? -1000000 : 0; // 3: out there, it does nothing of its own while a
}

void FlintSitePicture(int what, int index, int slot, int size)
{
	const int w = size & 0xFF;
	const int h = size >> 8 & 0xFF;
	const int picture = slot >= 0 && slot < SITE_SLOTS && w > 0 && h > 0 && w <= SITE_CELL_W && h <= SITE_CELL_H ? slot + 1 : 0;
	int i = -1;
	if (what == 0 && index >= 0 && index < 8)
		i = index;
	else if (what == 1 && index >= 0 && index < 3)
		i = arena.stack[index];
	else if (what == 2 && index >= 0 && index < 6)
	{
		arena.pool[index].picture = picture;
		arena.pool[index].w = w;
		arena.pool[index].h = h;
		return;
	}
	// Only for something still on its way in.
	if (i < 0 || i >= 8 || arena.incoming[i].wait <= 0 || arena.incoming[i].what != (what == 1 ? INCOMING_BUTTON : INCOMING_PIECE))
		return;
	arena.incoming[i].picture = picture;
	arena.incoming[i].w = w;
	arena.incoming[i].h = h;
}

void FlintSiteReload(void)
{
	ReloadBitmap_File("Npc/NpcSite", SURFACE_ID_FLINT_SITE);
}

int FlintBossRage(void)
{
	return arena.rage ? 1 : 0;
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

// ------------------------------------------------------------------ drawing

static unsigned long Colour(int r, int g, int b)
{
	return (unsigned long)r | ((unsigned long)g << 8) | ((unsigned long)b << 16);
}

// The End crystal's beam: a twisting purple and white line, as the dragon's.
static void DrawBeam(int x1, int y1, int x2, int y2, int fx, int fy, int seed)
{
	const float sx = (float)((x1 - fx) / 0x200), sy = (float)((y1 - fy) / 0x200);
	const float dx = (float)((x2 - fx) / 0x200) - sx, dy = (float)((y2 - fy) / 0x200) - sy;
	const float length = sqrtf(dx * dx + dy * dy);
	if (length < 4.0f)
		return;

	const float nx = -dy / length, ny = dx / length;
	for (float d = 0.0f; d < length; d += 2.0f)
	{
		const float t = d / length;
		for (int strand = 0; strand < 2; strand++)
		{
			const float wave = sinf(d * 0.21f - gCounter * 0.32f + seed + strand * 3.1416f) * 2.2f;
			const int x = (int)(sx + dx * t + nx * wave);
			const int y = (int)(sy + dy * t + ny * wave);
			RECT rect = {x, y, x + 1, y + 1};
			CortBox(&rect, strand ? Colour(0xB0, 0x3D, 0xF0) : Colour(0xF4, 0xB4, 0xFF));
		}
		if (((int)d / 2 + gCounter / 2) % 3 == 0)
		{
			const int x = (int)(sx + dx * t), y = (int)(sy + dy * t);
			RECT rect = {x, y, x + 1, y + 1};
			CortBox(&rect, Colour(0xFF, 0xF0, 0xFF));
		}
	}
}

void PutFlintExtras(int fx, int fy)
{
	NPCHAR *boss = &gBoss[0];
	if (!(boss->cond & 0x80) || boss->code_char != 10)
		return;

	// Its crystals' beams while it heals off them.
	if (boss->act_no == ACT_GUARD)
	{
		for (int i = 0; i < arena.crystalCount; i++)
		{
			NPCHAR *c = &gBoss[PART_CRYSTAL + i];
			if (c->cond & 0x80)
				DrawBeam(c->x, c->y - PX(6), boss->x, boss->y - PX(10), fx, fy, i * 2);
		}
	}

	// Its arm, from above the screen down to its hand.
	NPCHAR *hand = &gBoss[PART_HAND];
	if (arena.reach && (hand->cond & 0x80))
	{
		const int shake = hand->shock ? (hand->shock / 2 % 2 ? 1 : -1) : 0;
		const int x = (hand->x - fx) / 0x200 - 8 + shake;
		RECT segment = {144, 0, 160, 16};
		for (int y = (arena.top - PX(32) - fy) / 0x200; y < (hand->y - PX(14) - fy) / 0x200; y += 16)
			PutBitmap3(&grcGame, x, y, &segment, SURFACE_ID_LEVEL_SPRITESET_1);
		RECT palm = {arena.reach >= 3 && arena.reach <= 4 ? 112 : 80, 0, 0, 32};
		palm.right = palm.left + 32;
		PutBitmap3(&grcGame, (hand->x - fx) / 0x200 - 16 + shake, (hand->y - fy) / 0x200 - 16, &palm, SURFACE_ID_LEVEL_SPRITESET_1);
	}
}

// The player's own cursor, resting on it out on the site: its hand comes
// for them at once.
void FlintBossStare(void)
{
	NPCHAR *boss = &gBoss[0];
	if (!(boss->cond & 0x80) || boss->code_char != 10 || arena.phase != 3 || !arena.outside)
		return;
	if (arena.reach || arena.tiltWait > 0 || arena.stareCooldown > 0 || !(g_GameFlags & 2))
		return;
	arena.stareCooldown = 300;
	PlaySoundObject(108, 1);
	StartReach(gMC.x, 30);
	FlintEmit("stared", 0, 0);
}
