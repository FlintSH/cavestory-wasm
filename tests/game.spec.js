const {test,expect} = require('@playwright/test');
const state = page => page.evaluate(() => JSON.parse(caveStory.UTF8ToString(caveStory._web_state())));
const delay = ms => new Promise(resolve => setTimeout(resolve,ms));
async function start(page,lang='en') {
  await page.goto('/'); await page.selectOption('#language',lang); await page.click('#play');
  await expect(page.locator('#cover')).toBeHidden({timeout:30000});
  await page.locator('#canvas').focus();
}
async function newGame(page) {
  await delay(1000);
  // Held key presses must span at least one 20 ms game tick.
  for (let i=0;i<260;i++) {
    const current=await state(page);
    if(current.stage===13 && (current.flags&2)) { await delay(1000); return; }
    await page.keyboard.press('z',{delay:70});
    await delay(180);
  }
  throw new Error('Opening sequence did not reach playable Start Point');
}
for (const lang of ['en','jp']) test(`${lang}: original engine, controls and audio`,async({page})=>{
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.addInitScript(()=>{
    window.audioFrames=0;window.audioPeak=0;
    const proto=AudioContext.prototype, original=proto.createScriptProcessor;
    proto.createScriptProcessor=function(...args){
      const node=original.apply(this,args);let handler;
      Object.defineProperty(node,'onaudioprocess',{set(fn){handler=fn},get(){return handler}});
      node.addEventListener('audioprocess',event=>{
        if(handler)handler(event);
        const data=event.outputBuffer.getChannelData(0);window.audioFrames+=data.length;
        for(let i=0;i<data.length;i++)window.audioPeak=Math.max(window.audioPeak,Math.abs(data[i]));
      });return node;
    };
  });
  await start(page,lang);await newGame(page);
  console.log(lang,'start',await state(page));
  await page.screenshot({path:`docs/${lang}-gameplay.png`});
  const jumpStart=(await state(page)).y;
  await page.keyboard.down('z');await delay(120);await page.keyboard.up('z');
  expect((await state(page)).y).toBeLessThan(jumpStart);
  await delay(650);
  const before=await state(page);
  await page.keyboard.down('ArrowRight');await delay(150);await page.keyboard.up('ArrowRight');
  expect((await state(page)).x).toBeGreaterThan(before.x);
  await page.click('#pause');const frozen=await state(page);await delay(300);expect(await state(page)).toEqual(frozen);
  await page.click('#pause');
  await expect.poll(()=>page.evaluate(()=>window.audioPeak),{timeout:10000}).toBeGreaterThan(0);
  expect(await page.evaluate(()=>window.audioFrames)).toBeGreaterThan(0);
  console.log(lang,'after jump',await state(page));
  expect(errors).toEqual([]);
});
