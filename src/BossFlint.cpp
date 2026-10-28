// The enderman from fl1nt.dev's sidebar, who followed you into the cave to get
// its pearl back. Added by the cavestory-wasm project for fl1nt.dev's hidden
// fight (MIT); see docs/FLINT_FIGHT.md. Positions use the engine's fixed
// point, 0x200 per pixel. The arena (Stage/Flint.*) and the sprite sheet
// (Npc/NpcFlint.pbm) are written into /data by the website before startup.
#include "BossFlint.h"

#include <string.h>

#include "WindowsWrapper.h"

#include "Boss.h"
#include "Frame.h"
#include "Game.h"
#include "Map.h"
#include "MyChar.h"
#include "NpChar.h"
#include "Sound.h"

#define PX(n) ((n) * 0x200)

// One 20x15 screen: walls one tile thick, floor surface at y = 192 (three
// rows up, clear of the boss health bar).
static const int kLeft = PX(16 + 12);
static const int kRight = PX(304 - 12);
static const int kStandY = PX(192 - 24); // centre of a 48px body standing on the floor

static const int kLife = 100;
static const int kRageLife = 50;

// Sprite sheet frames: 32x48, facing left on the first row, right on the second.
enum
{
	FRAME_STAND,
	FRAME_WALK1,
	FRAME_WALK2,
	FRAME_STARE,
	FRAME_CARRY,
	FRAME_THROW,
	FRAME_FADE,
	FRAME_HIDDEN
};

static BOOL behind;  // reappear behind the player (they stared too long)
static BOOL raged;

static void SetFrame(NPCHAR *npc, int frame)
{
	if (frame == FRAME_HIDDEN)
	{
		npc->rect.left = npc->rect.right = npc->rect.top = npc->rect.bottom = 0;
		return;
	}

	npc->rect.left = frame * 32;
	npc->rect.right = frame * 32 + 32;
	npc->rect.top = npc->direct == 0 ? 0 : 48;
	npc->rect.bottom = npc->rect.top + 48;
}

static void FacePlayer(NPCHAR *npc)
{
	npc->direct = gMC.x < npc->x ? 0 : 2;
}

static void Clamp(NPCHAR *npc)
{
	if (npc->x < kLeft)
		npc->x = kLeft;
	if (npc->x > kRight)
		npc->x = kRight;
}

static void Puff(NPCHAR *npc, int count)
{
	for (int i = 0; i < count; i++)
		SetNpChar(4, npc->x + PX(Random(-8, 8)), npc->y + PX(Random(-20, 20)), Random(-0x200, 0x200), Random(-0x300, 0x100), 0, NULL, 0x100);
}

// Endermen hate being looked at.
static BOOL StaredAt(NPCHAR *npc)
{
	int dx = npc->x - gMC.x;
	int dy = npc->y - gMC.y;
	if (dy < -PX(48) || dy > PX(48) || dx < -PX(200) || dx > PX(200))
		return FALSE;

	return (gMC.direct == 2 && dx > 0) || (gMC.direct == 0 && dx < 0);
}

static void Shake(NPCHAR *npc)
{
	npc->x += npc->act_wait % 2 ? PX(1) : -PX(1);
}

// gBoss[1] is the pumpkin it carries. Shoot it down or dodge it.
static void ThrowBlock(NPCHAR *npc)
{
	NPCHAR *block = &gBoss[1];
	memset(block, 0, sizeof(*block));
	block->cond = 0x80;
	block->direct = npc->direct;
	block->x = npc->x + (npc->direct == 0 ? -PX(10) : PX(10));
	block->y = npc->y - PX(26);
	// Land near the player in about 45 ticks.
	block->xm = (gMC.x - block->x) / 45;
	if (block->xm > 0x600)
		block->xm = 0x600;
	if (block->xm < -0x600)
		block->xm = -0x600;
	block->ym = -0x480;
	block->life = 3;
	block->damage = 2;
	block->size = 1;
	block->hit_voice = 52;
	block->destroy_voice = 71;
	block->bits = npc_shootable | npc_ignoreSolid;
	block->hit.front = block->hit.back = block->hit.top = block->hit.bottom = PX(6);
	block->view.front = block->view.back = block->view.top = block->view.bottom = PX(8);
	PlaySoundObject(39, 1);
}

// Walls and floor are 0x41; the arena's platforms are 0x05 (solid, drawn behind).
static BOOL Solid(unsigned char attribute)
{
	return attribute == 0x41 || attribute == 0x05;
}

static void BreakBlock(NPCHAR *block)
{
	SetDestroyNpChar(block->x, block->y, PX(8), 4);
	PlaySoundObject(71, 1);
	block->cond = 0;
}

static void ActBlock(void)
{
	NPCHAR *block = &gBoss[1];
	if ((block->cond & 0x80) == 0)
		return;

	block->ym += 0x40;
	if (block->ym > 0x5FF)
		block->ym = 0x5FF;
	block->x += block->xm;
	block->y += block->ym;

	if (++block->ani_wait > 3)
	{
		block->ani_wait = 0;
		block->ani_no = (block->ani_no + 1) % 4;
	}
	block->rect.left = block->ani_no * 16;
	block->rect.right = block->rect.left + 16;
	block->rect.top = 96;
	block->rect.bottom = 112;

	// Smash on whatever it hits: walls, platforms or the floor.
	unsigned char below = GetAttribute(block->x / PX(16), (block->y + PX(6)) / PX(16));
	unsigned char ahead = GetAttribute((block->x + (block->xm > 0 ? PX(6) : -PX(6))) / PX(16), block->y / PX(16));
	if (Solid(below) || Solid(ahead) || block->y > PX(240))
		BreakBlock(block);
}

void ActBossChar_Enderman(void)
{
	NPCHAR *npc = gBoss;
	BOOL rage = npc->life <= kRageLife;

	switch (npc->act_no)
	{
		case 0:
			// Waits on the far side while the intro plays.
			npc->cond = 0x80;
			npc->x = PX(256);
			npc->y = kStandY;
			npc->direct = 0;
			npc->life = kLife;
			npc->exp = 1;
			npc->size = 3;
			npc->hit_voice = 52;
			npc->destroy_voice = 72;
			npc->code_event = 1000;
			npc->bits = npc_ignoreSolid | npc_eventDie | npc_showDamage;
			npc->hit.front = npc->hit.back = PX(8);
			npc->hit.top = PX(20);
			npc->hit.bottom = PX(24);
			npc->view.front = npc->view.back = PX(16);
			npc->view.top = npc->view.bottom = PX(24);
			behind = FALSE;
			raged = FALSE;
			npc->act_no = 1;
			// Fallthrough
		case 1:
			FacePlayer(npc);
			SetFrame(npc, FRAME_STAND);
			break;

		case 10:
			// <BOA0010: fight.
			npc->bits |= npc_shootable;
			npc->damage = 1;
			npc->act_no = 20;
			// Fallthrough
		case 20:
			npc->act_no = 21;
			npc->act_wait = 0;
			npc->count1 = rage ? Random(20, 40) : Random(40, 80);
			// Fallthrough
		case 21:
			if (rage && !raged)
			{
				raged = TRUE;
				npc->act_no = 50;
				break;
			}

			FacePlayer(npc);
			// Stalks you, but stops at arm's length: the hits come from its attacks.
			if (npc->x - gMC.x > PX(36) || gMC.x - npc->x > PX(36))
			{
				npc->xm = (rage ? 0x220 : 0x180) * (npc->direct == 0 ? -1 : 1);
				npc->x += npc->xm;
				Clamp(npc);
				SetFrame(npc, (npc->act_wait / 8) % 4 == 0 ? FRAME_WALK1 : (npc->act_wait / 8) % 4 == 2 ? FRAME_WALK2 : FRAME_STAND);
			}
			else
			{
				npc->xm = 0;
				SetFrame(npc, FRAME_STAND);
			}

			if (StaredAt(npc))
			{
				if (++npc->count2 > 60)
				{
					npc->count2 = 0;
					npc->act_no = 50;
					break;
				}
			}
			else if (npc->count2 > 0)
			{
				--npc->count2;
			}

			if (++npc->act_wait > npc->count1)
				npc->act_no = (gBoss[1].cond & 0x80) == 0 && Random(0, 2) != 0 ? 40 : 30;

			break;

		case 30:
			// Teleport out.
			npc->act_no = 31;
			npc->act_wait = 0;
			npc->xm = 0;
			npc->bits &= ~npc_shootable;
			npc->damage = 0;
			PlaySoundObject(29, 1);
			Puff(npc, 6);
			// Fallthrough
		case 31:
			SetFrame(npc, npc->act_wait < 8 && npc->act_wait % 2 ? FRAME_FADE : FRAME_HIDDEN);

			if (++npc->act_wait > (rage ? 16 : 28))
			{
				// Reappear beside the player; behind them if they stared.
				int side;
				if (behind)
					side = gMC.direct == 0 ? 1 : -1;
				else
					side = Random(0, 1) ? 1 : -1;

				behind = FALSE;
				npc->x = gMC.x + side * PX(56);
				if (npc->x < kLeft || npc->x > kRight)
					npc->x = gMC.x - side * PX(56);
				Clamp(npc);
				npc->y = kStandY;
				FacePlayer(npc);
				PlaySoundObject(29, 1);
				Puff(npc, 6);
				npc->act_no = 32;
				npc->act_wait = 0;
			}

			break;

		case 32:
			// Stare...
			SetFrame(npc, FRAME_STARE);

			if (++npc->act_wait == 4)
			{
				npc->bits |= npc_shootable;
				npc->damage = 1;
			}

			if (npc->act_wait > (rage ? 10 : 22))
			{
				FacePlayer(npc);
				npc->act_no = 33;
				npc->act_wait = 0;
			}

			break;

		case 33:
			// ...then lunge. This is the hit to dodge.
			npc->damage = 3;
			npc->xm = npc->direct == 0 ? -0x500 : 0x500;
			npc->x += npc->xm;
			Clamp(npc);
			SetFrame(npc, npc->act_wait / 3 % 2 ? FRAME_WALK1 : FRAME_WALK2);

			if (++npc->act_wait > 18)
			{
				npc->damage = 1;
				npc->act_no = 20;
			}

			break;

		case 40:
			// Lift the pumpkin...
			npc->act_no = 41;
			npc->act_wait = 0;
			npc->xm = 0;
			// Fallthrough
		case 41:
			FacePlayer(npc);
			SetFrame(npc, FRAME_CARRY);

			if (++npc->act_wait > (rage ? 14 : 26))
			{
				ThrowBlock(npc);
				npc->act_no = 42;
				npc->act_wait = 0;
			}

			break;

		case 42:
			// ...and throw it.
			SetFrame(npc, FRAME_THROW);

			if (++npc->act_wait > 16)
				npc->act_no = rage ? 30 : 20;

			break;

		case 50:
			// Scream, then teleport behind the player.
			npc->act_no = 51;
			npc->act_wait = 0;
			npc->xm = 0;
			SetQuake(30);
			PlaySoundObject(26, 1);
			// Fallthrough
		case 51:
			SetFrame(npc, npc->act_wait / 2 % 2 ? FRAME_STARE : FRAME_STAND);
			Shake(npc);

			if (++npc->act_wait > 30)
			{
				behind = TRUE;
				npc->act_no = 30;
			}

			break;

		case 100:
			// <BOA0100 from the death event.
			npc->act_no = 101;
			npc->act_wait = 0;
			npc->xm = 0;
			npc->bits &= ~npc_shootable;
			npc->damage = 0;
			if (gBoss[1].cond & 0x80)
				BreakBlock(&gBoss[1]);
			SetQuake(60);
			// Fallthrough
		case 101:
			SetFrame(npc, npc->act_wait / 2 % 2 ? FRAME_STARE : FRAME_FADE);
			Shake(npc);

			if (npc->act_wait % 6 == 0)
			{
				PlaySoundObject(44, 1);
				Puff(npc, 3);
			}

			if (++npc->act_wait > 100)
			{
				SetDestroyNpChar(npc->x, npc->y, PX(16), 24);
				PlaySoundObject(72, 1);
				npc->cond = 0;
			}

			break;
	}

	ActBlock();
}
