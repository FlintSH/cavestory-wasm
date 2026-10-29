// Browser integration added by the cavestory-wasm project (MIT).
#include <cstdio>
#include <emscripten.h>
#include <SDL.h>
#include "MyChar.h"
#include "Stage.h"
#include "Game.h"
#include "KeyControl.h"
#include "Flags.h"
#include "Web.h"
#include "BossFlint.h"
extern SDL_AudioDeviceID audioDevice;

int gWebFight = 0;

EM_JS(void, flint_emit_js, (const char *name, int a, int b), {
    if (typeof Module.onFlint === 'function') {
        try { Module.onFlint(UTF8ToString(name), a, b); } catch (error) { console.error(error); }
    }
});

void FlintEmit(const char *name, int a, int b) {
    if (gWebFight) flint_emit_js(name, a, b);
}

extern "C" {
// Call before main(): boot into fl1nt.dev's hidden fight instead of the game.
EMSCRIPTEN_KEEPALIVE void web_fight(int on) {
    gWebFight = on ? 1 : 0;
}
// Lets the page see script flags, e.g. the fight's victory flag.
EMSCRIPTEN_KEEPALIVE int web_flag(int no) {
    return (no < 0 || no >= 8000) ? 0 : GetNPCFlag(no);
}
// The page saw the player's own cursor resting on the enderman, out on the
// site: it reacts as it does to being stared at.
EMSCRIPTEN_KEEPALIVE void web_fight_stare() {
    FlintBossStare();
}
// The page's pictures of what the enderman takes off the site, so what lands
// in the room looks like what it took (FlintSitePicture in BossFlint.h).
EMSCRIPTEN_KEEPALIVE void web_site_picture(int what, int index, int slot, int size) {
    FlintSitePicture(what, index, slot, size);
}
// It rewrote Npc/NpcSite.pbm, the sheet they're in.
EMSCRIPTEN_KEEPALIVE void web_site_reload() {
    FlintSiteReload();
}
EMSCRIPTEN_KEEPALIVE void web_pause(int pause) {
    gKey = gKeyTrg = 0;
    if (pause) emscripten_pause_main_loop(); else emscripten_resume_main_loop();
    SDL_PauseAudioDevice(audioDevice, pause);
}
EMSCRIPTEN_KEEPALIVE const char *web_state() {
    static char state[256];
    std::snprintf(state, sizeof(state), "{\"stage\":%d,\"x\":%d,\"y\":%d,\"life\":%d,\"maxLife\":%d,\"flags\":%d,\"counter\":%d}",
        gStageNo, gMC.x, gMC.y, gMC.life, gMC.max_life, g_GameFlags, gCounter);
    return state;
}
EMSCRIPTEN_KEEPALIVE void web_key(int key, int down) {
    SDL_Event event = {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.type = event.type;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    SDL_PushEvent(&event);
}
}

#ifdef WEB_TEST_HOOKS
#include "Profile.h"
#include "TextScr.h"
#include "Map.h"
#include "Ending.h"
#include "Boss.h"
#include "BossFlint.h"
#include "NpChar.h"
#include "MycHit.h"
#include "NpcHit.h"
#include "MycParam.h"
extern CREDIT Credit;
extern "C" {
EMSCRIPTEN_KEEPALIVE int web_test_stage(int stage) {
    if (stage < 0 || stage >= 95) return 0;
    InitMyChar();
    gMC.cond = 0x80; gMC.life = gMC.max_life = 100;
    int loaded = TransferStage(stage, 0, 8, 8);
    StopTextScript();
    g_GameFlags = 3;
    return loaded;
}
EMSCRIPTEN_KEEPALIVE void web_test_step(int ticks) {
    for (int i=0;i<ticks;i++) { ActNpChar(); ActBossChar(); ActMyChar(FALSE); HitMyCharMap(); HitNpCharMap(); }
}
EMSCRIPTEN_KEEPALIVE void web_test_credit_step(int ticks) {
    for (int i=0;i<ticks;i++) { ActionCredit(); ActionStripper(); ActionIllust(); }
}
EMSCRIPTEN_KEEPALIVE int web_test_credits() { g_GameFlags |= 8; return StartCreditScript(); }
EMSCRIPTEN_KEEPALIVE void web_test_illustration(int n) { SetCreditIllust(n); }
// fl1nt.dev's hidden fight: inspect the enderman and set its life.
EMSCRIPTEN_KEEPALIVE const char *web_test_boss() {
    static char state[300];
    int crystals = 0, blocks = 0;
    for (int i = 5; i < 9; i++) if (gBoss[i].cond & 0x80) ++crystals;
    for (int i = 1; i < 4; i++) if (gBoss[i].cond & 0x80) ++blocks;
    const NPCHAR *hand = &gBoss[11]; // the hand it reaches in with in the last phase
    std::snprintf(state, sizeof(state), "{\"cond\":%d,\"act\":%d,\"x\":%d,\"y\":%d,\"life\":%d,\"phase\":%d,\"rage\":%d,\"crystals\":%d,\"blocks\":%d,\"bits\":%d,\"hand\":{\"cond\":%d,\"x\":%d,\"y\":%d,\"damage\":%d}}",
        gBoss[0].cond, gBoss[0].act_no, gBoss[0].x, gBoss[0].y, gBoss[0].life, FlintBossPhase(), FlintBossRage(), crystals, blocks, gBoss[0].bits,
        hand->cond, hand->x, hand->y, hand->damage);
    return state;
}
EMSCRIPTEN_KEEPALIVE void web_test_boss_life(int life) { gBoss[0].life = life; }
EMSCRIPTEN_KEEPALIVE void web_test_boss_phase(int phase) { FlintBossSetPhase(phase); }
// Move the enderman to a tile (while it's dying, say, so its pearl drops from there).
EMSCRIPTEN_KEEPALIVE void web_test_boss_move(int x, int y) {
    gBoss[0].x = x * 16 * 0x200; gBoss[0].y = y * 16 * 0x200; gBoss[0].xm = gBoss[0].ym = 0;
}
// Put the player on a tile (for walking QA through the level).
EMSCRIPTEN_KEEPALIVE void web_test_warp(int x, int y) {
    gMC.x = x * 16 * 0x200; gMC.y = y * 16 * 0x200; gMC.xm = gMC.ym = 0;
}
// Set the player's life and maximum.
EMSCRIPTEN_KEEPALIVE void web_test_life(int life) { gMC.life = gMC.max_life = life; }
// Give the current weapon energy, as if picked up (levels it, and at its
// maximum adds a Whimsical Star).
EMSCRIPTEN_KEEPALIVE void web_test_exp(int exp) { AddExpMyChar(exp); }
// Run a script event, e.g. a chest's.
EMSCRIPTEN_KEEPALIVE int web_test_event(int no) { return StartTextScript(no); }
// Set a boss part's condition (0 destroys it, as if shot down).
EMSCRIPTEN_KEEPALIVE void web_test_boss_part(int part, int cond) {
    if (part >= 0 && part < BOSS_MAX) gBoss[part].cond = cond;
}
// Where the first live NPC of a code is, in the engine's fixed point.
EMSCRIPTEN_KEEPALIVE const char *web_test_find(int code) {
    static char found[128];
    std::snprintf(found, sizeof(found), "null");
    for (int i = 0; i < NPC_MAX; i++)
        if ((gNPC[i].cond & 0x80) && gNPC[i].code_char == code) { std::snprintf(found, sizeof(found), "{\"x\":%d,\"y\":%d,\"surf\":%d,\"w\":%d,\"h\":%d}", gNPC[i].x, gNPC[i].y, (int)gNPC[i].surf, (int)(gNPC[i].rect.right - gNPC[i].rect.left), (int)(gNPC[i].rect.bottom - gNPC[i].rect.top)); break; }
    return found;
}
// How many NPCs of a code are alive.
EMSCRIPTEN_KEEPALIVE int web_test_count(int code) {
    int count = 0;
    for (int i = 0; i < NPC_MAX; i++) if ((gNPC[i].cond & 0x80) && gNPC[i].code_char == code) ++count;
    return count;
}
EMSCRIPTEN_KEEPALIVE int web_test_save() {
    gMC.x = 15 * 16 * 512; gMC.y = 7 * 16 * 512;
    gMC.life = gMC.max_life = 3;
    return SaveProfile(NULL);
}
EMSCRIPTEN_KEEPALIVE int web_test_load() { return LoadProfile(NULL); }
EMSCRIPTEN_KEEPALIVE const char *web_test_stats() {
    static char result[256];
    std::snprintf(result,sizeof(result),"{\"mapWidth\":%d,\"mapHeight\":%d,\"bossAction\":%d,\"bossCondition\":%d,\"creditOffset\":%d,\"creditMode\":%d}",gMap.width,gMap.length,gBoss[0].act_no,gBoss[0].cond,Credit.offset,Credit.mode);
    return result;
}
EMSCRIPTEN_KEEPALIVE void web_test_throw(int what, int a, int b) { FlintBossTestThrow(what, a, b); }
}
#endif
