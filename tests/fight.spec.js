const {test,expect}=require('@playwright/test');
// fl1nt.dev's hidden fight (docs/FLINT_FIGHT.md). The website supplies the
// arena's art; here a minimal arena is assembled from the game's own files.
test('hidden fight: boots into the arena, the enderman fights, and its death event runs',async({page})=>{
  test.skip(!process.env.QA_URL,'Build with --test-hooks --output build/qa and set QA_URL to its server.');
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  page.on('dialog',async dialog=>{errors.push('dialog: '+dialog.message());await dialog.dismiss();});
  await page.goto(process.env.QA_URL);
  await page.addScriptTag({url:new URL('cave-en.js',process.env.QA_URL+'/').href});
  await page.evaluate(async()=>{
    const canvas=document.querySelector('canvas');
    const game=await createCaveStory({canvas,noInitialRun:true,locateFile:file=>new URL(file,location.href).href});
    const fs=game.FS;
    const copy=(from,to)=>fs.writeFile(to,fs.readFile(from));
    copy('/data/Stage/PrtCave.pbm','/data/Stage/PrtFlint.pbm');
    copy('/data/Stage/Cave.pxa','/data/Stage/Flint.pxa');
    copy('/data/bkBlue.pbm','/data/bkFlint.pbm');
    copy('/data/Npc/NpcGuest.pbm','/data/Npc/NpcFlint.pbm');
    // 20x15 box: tile 1 of the Cave tileset is solid (0x41); the floor is row 12.
    const pxm=new Uint8Array(8+20*15);pxm.set([0x50,0x58,0x4d,0x10,20,0,15,0]);
    for(let y=0;y<15;y++)for(let x=0;x<20;x++)pxm[8+y*20+x]=(x===0||x===19||y===0||y>=12)?1:0;
    fs.writeFile('/data/Stage/Flint.pxm',pxm);
    fs.writeFile('/data/Stage/Flint.pxe',new Uint8Array([0x50,0x58,0x45,0,0,0,0,0]));
    // Event 100 arms the player and starts the fight; 1000 is the death event.
    const text='#0100\r\n<AM+0002:0000<BOA0010<FRE<END\r\n#1000\r\n<BOA0100<FL+7777<END\r\n';
    const plain=Uint8Array.from(text,c=>c.charCodeAt(0));const half=plain.length>>1;const key=plain[half];
    fs.writeFile('/data/Stage/Flint.tsc',plain.map((b,i)=>i===half?b:(b+key)&255));
    window.caveStory=game;
    game._web_fight(1);
    game.callMain([]);
    canvas.focus();
  });
  const state=()=>page.evaluate(()=>({me:JSON.parse(caveStory.UTF8ToString(caveStory._web_state())),boss:JSON.parse(caveStory.UTF8ToString(caveStory._web_test_boss())),won:caveStory._web_flag(7777)}));
  // Straight into the arena: no opening, no title.
  await expect.poll(async()=>(await state()).me.stage,{timeout:10000}).toBe(95);
  await expect.poll(async()=>(await state()).boss.act,{timeout:10000}).toBeGreaterThanOrEqual(20);
  let s=await state();
  expect(s.boss.life).toBe(100);
  expect(s.won).toBe(0);
  // One hit left: face it and shoot.
  await page.evaluate(()=>caveStory._web_test_boss_life(1));
  await expect.poll(async()=>{
    s=await state();
    if(!s.won){await page.keyboard.press(s.boss.x<s.me.x?'ArrowLeft':'ArrowRight');await page.keyboard.press('KeyX');}
    return s.won;
  },{timeout:20000,intervals:[100]}).toBe(1);
  // The death animation then removes the enderman.
  await expect.poll(async()=>(await state()).boss.cond,{timeout:10000}).toBe(0);
  expect(errors).toEqual([]);
});
