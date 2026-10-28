const {test,expect}=require('@playwright/test');
// fl1nt.dev's hidden level (docs/FLINT_FIGHT.md). The website draws the real
// level; here a one-room level is assembled from the game's own files.
test('hidden level: boots into stage 95, the enderman goes through its phases, and its pearl ends the fight',async({page})=>{
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
    copy('/data/bkBlue.pbm','/data/bkFlint.pbm');
    copy('/data/Npc/NpcGuest.pbm','/data/Npc/NpcVoid.pbm');
    copy('/data/Npc/NpcGuest.pbm','/data/Npc/NpcFlint.pbm');
    // Tile 1 is a wall (0x41), tile 2 a platform (0x05).
    const pxa=new Uint8Array(256);pxa[1]=0x41;pxa[2]=0x05;
    fs.writeFile('/data/Stage/Flint.pxa',pxa);
    // One 21x16 room: ceiling row 0, walls at columns 0 and 20, floor from row 12,
    // two low platforms on row 8 and a perch on row 5.
    const w=21,h=16,pxm=new Uint8Array(8+w*h);pxm.set([0x50,0x58,0x4d,0x10,w,0,h,0]);
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){
      let t=(x===0||x===20||y===0||y>=12)?1:0;
      if(y===8&&((x>=4&&x<=6)||(x>=14&&x<=16)))t=2;
      if(y===5&&x>=8&&x<=12)t=2;
      pxm[8+y*w+x]=t;
    }
    fs.writeFile('/data/Stage/Flint.pxm',pxm);
    // Entities: where you arrive (90), the room's centre (300), the boss's spot
    // (330), its perch (320), two crystals (310, 311), and an endermite (361).
    const ents=[[4,11,0,90,0],[10,7,0,300,0],[15,11,0,330,0],[10,4,0,320,0],[2,11,0,310,0],[18,11,0,311,0],[7,11,0,0,361]];
    const pxe=new Uint8Array(8+ents.length*12);const view=new DataView(pxe.buffer);
    pxe.set([0x50,0x58,0x45,0]);view.setUint32(4,ents.length,true);
    ents.forEach(([x,y,flag,event,code],i)=>[x,y,flag,event,code,0].forEach((v,j)=>view.setUint16(8+i*12+j*2,v,true)));
    fs.writeFile('/data/Stage/Flint.pxe',pxe);
    // #0100 arms the player (with life to spare) and starts the fight; #1000
    // is the death; #1100 the pearl.
    const text=['#0090','<END','#0100','<KEY<AM+0002:0000<ML+0050<LI+0100<BOA0010<WAI0005<BOA0100<FRE<END','#1000','<KEY<BOA0500<WAI0170<FRE<END',
      '#1010','<END','#1020','<END','#1100','<KEY<DNP1100<FL+7777<FRE<END',''].join('\r\n');
    const plain=Uint8Array.from(text,c=>c.charCodeAt(0));const half=plain.length>>1;const key=plain[half];
    fs.writeFile('/data/Stage/Flint.tsc',plain.map((b,i)=>i===half?b:(b+key)&255));
    window.caveStory=game;
    game._web_fight(1);
    game.callMain([]);
    canvas.focus();
  });
  const state=()=>page.evaluate(()=>({me:JSON.parse(caveStory.UTF8ToString(caveStory._web_state())),boss:JSON.parse(caveStory.UTF8ToString(caveStory._web_test_boss())),won:caveStory._web_flag(7777)}));
  // Straight into the level, at its arrival marker, with the new NPC codes loaded.
  await expect.poll(async()=>(await state()).me.stage,{timeout:10000}).toBe(95);
  let s=await state();
  expect(s.me.x>>13).toBe(4);
  expect(await page.evaluate(()=>caveStory._web_test_count(361))).toBe(1);
  await expect.poll(async()=>(await state()).boss.act,{timeout:10000}).toBeGreaterThanOrEqual(110);
  s=await state();
  expect([s.boss.life,s.boss.phase]).toEqual([300,1]);
  // Second phase: crystals that heal it until they're gone.
  await page.evaluate(()=>caveStory._web_test_boss_life(200));
  await expect.poll(async()=>(await state()).boss.crystals,{timeout:15000}).toBe(2);
  s=await state();
  expect(s.boss.phase).toBe(2);
  await expect.poll(async()=>(await state()).boss.act,{timeout:5000}).toBe(210);
  await page.evaluate(()=>caveStory._web_test_boss_life(150));
  await expect.poll(async()=>(await state()).boss.life,{timeout:5000}).toBeGreaterThan(150);
  await page.evaluate(()=>{caveStory._web_test_boss_part(5,0);caveStory._web_test_boss_part(6,0);});
  await expect.poll(async()=>(await state()).boss.act,{timeout:10000}).toBe(220);
  // Rage, then one hit left: get in range, face it and shoot.
  await page.evaluate(()=>caveStory._web_test_boss_life(100));
  await expect.poll(async()=>(await state()).boss.phase,{timeout:15000}).toBe(3);
  await page.evaluate(()=>caveStory._web_test_boss_life(1));
  const hold=async(key,ms)=>{await page.keyboard.down(key);await page.waitForTimeout(ms);await page.keyboard.up(key);};
  await expect.poll(async()=>{
    s=await state();
    expect(s.me.life).toBeGreaterThan(0);
    if(s.boss.act<500&&s.boss.cond){
      const toward=s.boss.x<s.me.x?'ArrowLeft':'ArrowRight';
      await hold(toward,Math.abs(s.boss.x-s.me.x)>64*0x200?120:30);
      await hold('KeyX',60);
    }
    return s.boss.act>=500||!s.boss.cond;
  },{timeout:45000,intervals:[50]}).toBe(true);
  // It comes apart and drops its pearl; walking into the pearl runs #1100.
  await expect.poll(()=>page.evaluate(()=>caveStory._web_test_count(365)),{timeout:10000}).toBe(1);
  await expect.poll(async()=>{
    s=await state();
    if(!s.won){
      const pearl=await page.evaluate(()=>JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(365))));
      if(pearl)await hold(pearl.x<s.me.x?'ArrowLeft':'ArrowRight',60);
    }
    return s.won;
  },{timeout:30000,intervals:[100]}).toBe(1);
  expect(errors).toEqual([]);
});
