// Creatures and effects for fl1nt.dev's hidden level, /dev/null. Added by the
// cavestory-wasm project (MIT); see docs/FLINT_FIGHT.md. The sprites are on
// the stage's NPC sheets, which the website writes into /data:
//
// Npc/NpcVoid.pbm (sheet 1)
//   y = 0     endermite, 16x16: idle 1-2, crouch, rising, falling (left row, then right row)
//   y = 32    stray cursors, 16x16: drift 1-3, aim, dive; three colours, each a left and a right row
//   y = 128   motes, 8x8: four colours of four frames
//   y = 136   ender pearl, 16x16: six frames
//   y = 152   end rod, 16x16: two frames
//   y = 168   end gateway, 32x48: four frames
// Npc/NpcFlint.pbm (sheet 2, the boss's)
//   y = 192, x = 64: block shards, 8x8: two materials of four frames
#include "NpcFlint.h"

#include <stddef.h>

#include "WindowsWrapper.h"

#include "Caret.h"
#include "Game.h"
#include "MyChar.h"
#include "NpChar.h"
#include "NpcTbl.h"
#include "Sound.h"
#include "Triangle.h"

#define PX(n) ((n) * 0x200)

// Table rows for codes 361-368, in npc.tbl's layout: bits, life, sheet,
// hit sound, death sound, smoke size, experience, damage, hit box, view box.
// The endermite and the stray cursor take the First Cave critter's and bat's
// sounds.
const NPC_TABLE gFlintNpcTable[NPC_FLINT_COUNT] = {
	{npc_showDamage | npc_shootable | npc_solidSoft, 4, SURFACE_ID_LEVEL_SPRITESET_1, 51, 71, 2, 3, 1, {6, 1, 6, 8}, {8, 8, 8, 8}}, // endermite
	{npc_showDamage | npc_shootable, 3, SURFACE_ID_LEVEL_SPRITESET_1, 50, 70, 1, 2, 1, {5, 5, 5, 5}, {8, 8, 8, 8}}, // stray cursor
	{npc_ignoreSolid, 1, SURFACE_ID_LEVEL_SPRITESET_1, 0, 0, 0, 0, 0, {0, 0, 0, 0}, {0, 0, 0, 0}}, // mote emitter
	{npc_ignoreSolid, 1, SURFACE_ID_LEVEL_SPRITESET_1, 0, 0, 0, 0, 0, {0, 0, 0, 0}, {4, 4, 4, 4}}, // mote
	{npc_eventTouch, 1, SURFACE_ID_LEVEL_SPRITESET_1, 0, 0, 0, 0, 0, {6, 6, 6, 8}, {8, 8, 8, 8}}, // ender pearl
	{npc_eventTouch | npc_ignoreSolid, 1, SURFACE_ID_LEVEL_SPRITESET_1, 0, 0, 0, 0, 0, {8, 16, 8, 24}, {16, 24, 16, 24}}, // end gateway
	{npc_ignoreSolid, 1, SURFACE_ID_LEVEL_SPRITESET_1, 0, 0, 0, 0, 0, {0, 0, 0, 0}, {8, 8, 8, 8}}, // end rod
	{npc_ignoreSolid, 1, SURFACE_ID_LEVEL_SPRITESET_2, 0, 0, 0, 0, 0, {0, 0, 0, 0}, {4, 4, 4, 4}}, // block shard
	{npc_shootable, 3, SURFACE_ID_LEVEL_SPRITESET_1, 31, 12, 1, 1, 3, {6, 4, 6, 6}, {8, 8, 8, 8}}, // page piece
};

static void SetRect(NPCHAR *npc, int left, int top, int width, int height)
{
	npc->rect.left = left;
	npc->rect.top = top;
	npc->rect.right = left + width;
	npc->rect.bottom = top + height;
}

static BOOL PlayerNear(NPCHAR *npc, int dx, int dy)
{
	return npc->x - PX(dx) < gMC.x && npc->x + PX(dx) > gMC.x && npc->y - PX(dy) < gMC.y && npc->y + PX(dy) > gMC.y;
}

void SetMoteBurst(int x, int y, int count, int speed, int colour)
{
	for (int i = 0; i < count; i++)
	{
		unsigned char deg = (unsigned char)Random(0, 0xFF);
		int v = Random(speed / 2, speed);
		SetNpChar(NPC_MOTE, x, y, GetCos(deg) * v / 0x200, GetSin(deg) * v / 0x200, colour, NULL, 0x100);
	}
}

// Endermite: hops at you like First Cave's critters, and skitters about
// between hops.
void ActNpc361(NPCHAR *npc)
{
	enum { IDLE1, IDLE2, CROUCH, RISE, FALL };

	switch (npc->act_no)
	{
		case 0:
			npc->act_no = 1;
			npc->act_wait = Random(0, 30);
			// Fallthrough
		case 1: // On the ground
			if (++npc->ani_wait > 7)
			{
				npc->ani_wait = 0;
				npc->ani_no = npc->ani_no == IDLE1 ? IDLE2 : IDLE1;
			}

			// Skitter a little, as mites do.
			if (npc->count1 > 0)
			{
				--npc->count1;
				npc->xm = npc->direct == 0 ? -0x100 : 0x100;
			}
			else
			{
				npc->xm = 0;
				if (Random(0, 90) == 0)
				{
					npc->count1 = Random(6, 14);
					npc->direct = Random(0, 1) ? 0 : 2;
				}
			}

			if (npc->act_wait > 0)
				--npc->act_wait;

			if (npc->shock || (npc->act_wait == 0 && PlayerNear(npc, 112, 64)))
			{
				npc->direct = gMC.x < npc->x ? 0 : 2;
				npc->act_no = 2;
				npc->act_wait = 0;
				npc->xm = 0;
				npc->count1 = 0;
			}

			break;

		case 2: // Crouch
			npc->ani_no = CROUCH;

			if (++npc->act_wait > 8)
			{
				npc->act_no = 3;
				npc->ym = -0x500 - Random(0, 0x100);
				npc->xm = (npc->direct == 0 ? -1 : 1) * (0x180 + Random(0, 0x100));
				PlaySoundObject(30, 1);
			}

			break;

		case 3: // Airborne
			npc->ani_no = npc->ym < 0 ? RISE : FALL;

			if (npc->flag & 5)
				npc->xm = -npc->xm / 2;

			if (npc->flag & 8 && npc->ym >= 0)
			{
				npc->act_no = 1;
				npc->act_wait = Random(25, 55);
				npc->ani_no = IDLE1;
				npc->xm = 0;
				PlaySoundObject(23, 1);
			}

			break;
	}

	npc->ym += 0x40;
	if (npc->ym > 0x5FF)
		npc->ym = 0x5FF;

	npc->x += npc->xm;
	npc->y += npc->ym;

	SetRect(npc, npc->ani_no * 16, npc->direct == 0 ? 0 : 16, 16, 16);
}

// Stray cursor: a visitor's pointer that fell out of fl1nt.dev. It drifts on
// the spot like First Cave's bats, then clicks: a quick dive at you.
void ActNpc362(NPCHAR *npc)
{
	enum { DRIFT1, DRIFT2, DRIFT3, AIM, DIVE };
	static const int kFlap[4] = {DRIFT1, DRIFT2, DRIFT3, DRIFT2};

	switch (npc->act_no)
	{
		case 0:
			npc->tgt_x = npc->x;
			npc->tgt_y = npc->y;
			npc->count1 = Random(0, 0xFF);
			npc->count2 = (npc->x / PX(16)) % 3; // colour
			npc->act_wait = Random(0, 40);
			npc->act_no = 1;
			// Fallthrough
		case 1: // Drift
			npc->count1 += 2;
			npc->x = npc->tgt_x + GetCos((unsigned char)npc->count1) * 8;
			npc->y = npc->tgt_y + GetSin((unsigned char)(npc->count1 * 2)) * 4;
			npc->direct = gMC.x < npc->x ? 0 : 2;

			if (++npc->ani_wait > 3)
			{
				npc->ani_wait = 0;
				npc->ani_no = (npc->ani_no + 1) % 4;
			}

			if (++npc->act_wait > 60 && PlayerNear(npc, 120, 88))
			{
				npc->act_no = 2;
				npc->act_wait = 0;
				PlaySoundObject(1, 1);
			}

			break;

		case 2: // Aim: hold still, then click
			npc->xm = 0;
			npc->ym = 0;
			npc->x += npc->act_wait % 4 < 2 ? PX(1) / 2 : -PX(1) / 2;

			if (++npc->act_wait > 18)
			{
				unsigned char deg = GetArktan(npc->x - gMC.x, npc->y - gMC.y);
				npc->xm = GetCos(deg) * 4;
				npc->ym = GetSin(deg) * 4;
				npc->act_no = 3;
				npc->act_wait = 0;
			}

			break;

		case 3: // Dive
			if (++npc->act_wait > 32 || npc->flag & 0xF)
			{
				npc->act_no = 4;
				npc->act_wait = 0;
			}

			break;

		case 4: // Float back
		{
			unsigned char deg = GetArktan(npc->x - npc->tgt_x, npc->y - npc->tgt_y);
			npc->xm = GetCos(deg) * 3 / 2;
			npc->ym = GetSin(deg) * 3 / 2;

			if (++npc->ani_wait > 3)
			{
				npc->ani_wait = 0;
				npc->ani_no = (npc->ani_no + 1) % 4;
			}

			if ((npc->x - npc->tgt_x) / PX(1) * ((npc->x - npc->tgt_x) / PX(1)) + (npc->y - npc->tgt_y) / PX(1) * ((npc->y - npc->tgt_y) / PX(1)) < 36 || npc->act_wait++ > 150)
			{
				npc->tgt_x = npc->x - GetCos((unsigned char)npc->count1) * 8;
				npc->tgt_y = npc->y - GetSin((unsigned char)(npc->count1 * 2)) * 4;
				npc->xm = 0;
				npc->ym = 0;
				npc->act_no = 1;
				npc->act_wait = 0;
			}

			break;
		}
	}

	if (npc->act_no >= 3)
	{
		npc->x += npc->xm;
		npc->y += npc->ym;
	}

	int frame = npc->act_no == 2 ? AIM : npc->act_no == 3 ? DIVE : kFlap[npc->ani_no % 4];
	SetRect(npc, frame * 16, 32 + npc->count2 * 32 + (npc->direct == 0 ? 0 : 16), 16, 16);
}

// Invisible: lets motes rise from the ground around it while you're nearby.
void ActNpc363(NPCHAR *npc)
{
	SetRect(npc, 0, 0, 0, 0);

	if (!PlayerNear(npc, 360, 260))
		return;

	if (--npc->act_wait > 0)
		return;

	npc->act_wait = Random(14, 40);
	SetNpChar(NPC_MOTE, npc->x + PX(Random(-40, 40)), npc->y + PX(Random(-4, 12)), 0, 0, npc->direct == 2 ? MOTE_WHITE : (Random(0, 4) ? MOTE_PURPLE : MOTE_PINK), NULL, 0x100);
}

// Mote: a speck of the void's purple light, as around Minecraft's endermen.
// Still ones drift up; thrown ones slow down; travelling ones (beams) don't.
void ActNpc364(NPCHAR *npc)
{
	if (npc->act_no == 0)
	{
		npc->act_no = 1;
		npc->count2 = npc->direct & 3;

		if (npc->xm == 0 && npc->ym == 0)
		{
			npc->act_no = 2;
			npc->ym = -Random(0x30, 0x90);
			npc->count1 = Random(70, 140);
			npc->tgt_x = Random(0, 0xFF);
		}
		else if (npc->direct & MOTE_TRAVEL)
		{
			npc->act_no = 3;
			npc->count1 = Random(26, 32);
		}
		else
		{
			npc->count1 = Random(24, 48);
		}

		npc->act_wait = npc->count1;
	}

	switch (npc->act_no)
	{
		case 1: // Thrown
			npc->xm = npc->xm * 15 / 16;
			npc->ym = npc->ym * 15 / 16;
			break;

		case 2: // Drifting up
			npc->tgt_x += 3;
			npc->xm = GetSin((unsigned char)npc->tgt_x) / 4;
			break;
	}

	npc->x += npc->xm;
	npc->y += npc->ym;

	int left = --npc->act_wait;
	if (left <= 0)
	{
		npc->cond = 0;
		return;
	}

	int age = npc->count1 - left;
	int frame;
	if (left < 8)
		frame = 3;
	else if (left < 18)
		frame = 2;
	else if (age < 4)
		frame = 1;
	else
		frame = (age / 4) % 3 == 0 ? 0 : 1;

	SetRect(npc, (npc->count2 * 4 + frame) * 8, 128, 8, 8);
}

// The Ender Pearl the enderman drops. Touching it runs its event.
void ActNpc365(NPCHAR *npc)
{
	switch (npc->act_no)
	{
		case 0: // Falling (it pops up when dropped)
			// Given a floor (tgt_y), it passes through anything above it.
			if (npc->tgt_y != 0 && npc->y < npc->tgt_y - PX(12))
				npc->bits |= npc_ignoreSolid;
			else
				npc->bits &= ~npc_ignoreSolid;

			npc->ym += 0x20;
			if (npc->ym > 0x400)
				npc->ym = 0x400;

			if (npc->flag & 8)
			{
				if (npc->count1++ < 2 && npc->ym > 0x100)
				{
					npc->ym = -npc->ym / 2;
					PlaySoundObject(45, 1);
				}
				else
				{
					npc->ym = 0;
					npc->act_no = 1;
					npc->tgt_y = npc->y - PX(5);
					npc->bits |= npc_ignoreSolid;
				}
			}

			npc->y += npc->ym;
			break;

		case 1: // Hovering
			npc->count2 += 4;
			npc->y = npc->tgt_y + GetSin((unsigned char)npc->count2) * 2;

			if (++npc->act_wait % 22 == 0)
				SetNpChar(NPC_MOTE, npc->x + PX(Random(-5, 5)), npc->y, 0, 0, Random(0, 1) ? MOTE_PINK : MOTE_WHITE, NULL, 0x100);

			break;
	}

	if (++npc->ani_wait > 4)
	{
		npc->ani_wait = 0;
		npc->ani_no = (npc->ani_no + 1) % 6;
	}

	SetRect(npc, npc->ani_no * 16, 136, 16, 16);
}

// The End gateway home. Placed on a tile, it stands on the one below.
void ActNpc366(NPCHAR *npc)
{
	if (npc->act_no == 0)
	{
		npc->act_no = 1;
		npc->y -= PX(16);
		// Opened by a script (<CNP), it appears with a burst. Placed in the
		// map with the alternate direction, it has always been there.
		if (npc->direct != 2)
		{
			SetMoteBurst(npc->x, npc->y, 16, 0x500, MOTE_WHITE);
			PlaySoundObject(29, 1);
		}
	}

	if (++npc->ani_wait > 3)
	{
		npc->ani_wait = 0;
		npc->ani_no = (npc->ani_no + 1) % 4;
	}

	if (++npc->act_wait % 5 == 0)
		SetNpChar(NPC_MOTE, npc->x + PX(Random(-10, 10)), npc->y + PX(Random(4, 20)), 0, -Random(0x200, 0x400), Random(0, 2) ? MOTE_WHITE : MOTE_PURPLE, NULL, 0x100);

	SetRect(npc, npc->ani_no * 32, 168, 32, 48);
}

// End rod: a little light that flickers and sheds motes.
void ActNpc367(NPCHAR *npc)
{
	if (npc->act_no == 0)
	{
		npc->act_no = 1;
		npc->act_wait = Random(20, 120);
	}

	npc->ani_no = Random(0, 12) == 0 ? 1 : 0;

	if (--npc->act_wait <= 0)
	{
		npc->act_wait = Random(50, 130);
		SetNpChar(NPC_MOTE, npc->x, npc->y - PX(6), 0, 0, MOTE_WHITE, NULL, 0x100);
	}

	SetRect(npc, npc->ani_no * 16, 152, 16, 16);
}

// A piece of a block the enderman threw. Direction picks the material.
void ActNpc368(NPCHAR *npc)
{
	if (npc->act_no == 0)
	{
		npc->act_no = 1;
		npc->count1 = npc->direct % 3;
		npc->act_wait = Random(28, 44);
		// Torn-up page comes off fl1nt.dev's own sheet.
		if (npc->count1 == SHARD_PAGE)
			npc->surf = SURFACE_ID_LEVEL_SPRITESET_1;
	}

	npc->ym += 0x24;
	if (npc->ym > 0x500)
		npc->ym = 0x500;

	npc->x += npc->xm;
	npc->y += npc->ym;

	if (++npc->ani_wait > 2)
	{
		npc->ani_wait = 0;
		npc->ani_no = (npc->ani_no + 1) % 4;
	}

	if (--npc->act_wait <= 0)
	{
		npc->cond = 0;
		return;
	}

	if (npc->count1 == SHARD_PAGE)
		SetRect(npc, 80 + npc->ani_no * 8, 64, 8, 8);
	else
		SetRect(npc, 64 + (npc->count1 * 4 + npc->ani_no) * 8, 192, 8, 8);
}

// A piece of fl1nt.dev the enderman tore off the site and threw in, through
// the top of the screen: it falls, and breaks where it lands. Shooting it
// breaks it too. Its direction is which piece it is (PIECE_*).
static const struct { int x, y, w, h; } kPieces[PIECE_KINDS] = {
	{80, 32, 32, 24}, // a card
	{112, 32, 24, 12}, // a nav link
	{136, 32, 48, 16}, // the logo
	{184, 32, 24, 24}, // the avatar
	{208, 32, 32, 16}, // a visit counter
	{240, 32, 24, 8}, // a badge
};

void ActNpc369(NPCHAR *npc)
{
	const int kind = npc->direct % PIECE_KINDS;

	if (npc->act_no == 0)
	{
		npc->act_no = 1;
		npc->view.front = npc->view.back = PX(kPieces[kind].w / 2);
		npc->view.top = npc->view.bottom = PX(kPieces[kind].h / 2);
		npc->hit.front = npc->hit.back = PX(kPieces[kind].w / 2 - 2);
		npc->hit.top = npc->hit.bottom = PX(kPieces[kind].h / 2 - 1);
	}

	npc->ym += 0x28;
	if (npc->ym > 0x600)
		npc->ym = 0x600;
	npc->x += npc->xm;
	npc->y += npc->ym;

	// Breaks on the floor, a platform or a wall.
	if (npc->flag & 0xD)
	{
		for (int i = 0; i < 6; i++)
			SetNpChar(NPC_BLOCK_SHARD, npc->x + PX(Random(-8, 8)), npc->y + PX(Random(-4, 4)), Random(-0x300, 0x300), Random(-0x500, -0x100), SHARD_PAGE, NULL, 0x100);
		PlaySoundObject(12, 1);
		npc->cond = 0;
		return;
	}

	SetRect(npc, kPieces[kind].x, kPieces[kind].y, kPieces[kind].w, kPieces[kind].h);
}
