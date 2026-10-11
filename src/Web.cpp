// Browser integration added by the cavestory-wasm project (MIT).
#include <cstdio>
#include <emscripten.h>
#include <SDL.h>
#include "MyChar.h"
#include "Stage.h"
#include "Game.h"
#include "KeyControl.h"
extern SDL_AudioDeviceID audioDevice;
extern "C" {
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
#include "MycHit.h"
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
    for (int i=0;i<ticks;i++) { ActNpChar(); ActBossChar(); ActMyChar(FALSE); HitMyCharMap(); }
}
EMSCRIPTEN_KEEPALIVE void web_test_credit_step(int ticks) {
    for (int i=0;i<ticks;i++) { ActionCredit(); ActionStripper(); ActionIllust(); }
}
EMSCRIPTEN_KEEPALIVE int web_test_credits() { g_GameFlags |= 8; return StartCreditScript(); }
EMSCRIPTEN_KEEPALIVE void web_test_illustration(int n) { SetCreditIllust(n); }
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
}
#endif
