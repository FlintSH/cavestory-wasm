const {test,expect}=require('@playwright/test');
// fl1nt.dev's hidden level (docs/FLINT_FIGHT.md). The website draws the real
// level; here a one-room level is assembled from the game's own files.

// Boots fight mode into that level. cutscene is event 1020, the script for
// the cutscene before the enderman's last phase.
async function boot(page,errors,cutscene){
  page.on('pageerror',e=>errors.push(e.message));
  page.on('dialog',async dialog=>{errors.push('dialog: '+dialog.message());await dialog.dismiss();});
  await page.goto(process.env.QA_URL);
  await page.addScriptTag({url:new URL('cave-en.js',process.env.QA_URL+'/').href});
  await page.evaluate(async(cutscene)=>{
    const canvas=document.querySelector('canvas');
    const game=await createCaveStory({canvas,noInitialRun:true,locateFile:file=>new URL(file,location.href).href});
    const fs=game.FS;
    const copy=(from,to)=>fs.writeFile(to,fs.readFile(from));
    const encode=(text)=>{const plain=Uint8Array.from(text,c=>c.charCodeAt(0));const half=plain.length>>1;const key=plain[half];return plain.map((b,i)=>i===half?b:(b+key)&255);};
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
    // lost+found (stage 96): the same room, with nothing in it.
    copy('/data/Stage/PrtCave.pbm','/data/Stage/PrtFound.pbm');
    fs.writeFile('/data/Stage/Found.pxa',pxa);
    fs.writeFile('/data/Stage/Found.pxm',pxm);
    const none=new Uint8Array(8);none.set([0x50,0x58,0x45,0]);
    fs.writeFile('/data/Stage/Found.pxe',none);
    fs.writeFile('/data/Stage/Found.tsc',encode(['#0100','<FRE<END','#0101','<KEY<TRA0095:0100:0004:0011',''].join('\r\n')));
    // #0100 arms the player (with life to spare) and starts the fight; #0600
    // goes to lost+found; #1000 is the death; #1100 the pearl.
    fs.writeFile('/data/Stage/Flint.tsc',encode(['#0090','<END','#0100','<KEY<AM+0002:0000<ML+0050<LI+0100<BOA0010<WAI0005<BOA0100<FRE<END',
      '#0600','<KEY<TRA0096:0100:0005:0011','#1000','<KEY<BOA0500<WAI0170<FRE<END',
      '#1010','<END','#1020',cutscene,'#1100','<KEY<DNP1100<FL+7777<FRE<END',''].join('\r\n')));
    // Its last form's sheet: any picture no bigger than its surface will do.
    copy('/data/Npc/NpcGuest.pbm','/data/Npc/NpcFlintRage.pbm');
    window.caveStory=game;
    window.flint=[];
    window.said=[];
    game.onFlint=(name,a,b)=>{flint.push(name);said.push([name,a,b]);};
    game._web_fight(1);
    game.callMain([]);
    canvas.focus();
  },cutscene);
  const state=()=>page.evaluate(()=>({me:JSON.parse(caveStory.UTF8ToString(caveStory._web_state())),boss:JSON.parse(caveStory.UTF8ToString(caveStory._web_test_boss())),won:caveStory._web_flag(7777)}));
  // Straight into the level, at its arrival marker, with the new NPC codes loaded.
  await expect.poll(async()=>(await state()).me.stage,{timeout:10000}).toBe(95);
  return state;
}

// From the start of the fight to its second phase's end: it grows crystals
// that heal it, they're destroyed and it's knocked down.
async function toStunned(page,state){
  await expect.poll(async()=>(await state()).boss.act,{timeout:10000}).toBeGreaterThanOrEqual(110);
  const s=await state();
  expect([s.boss.life,s.boss.phase]).toEqual([300,1]);
  await page.evaluate(()=>caveStory._web_test_boss_life(200));
  await expect.poll(async()=>(await state()).boss.crystals,{timeout:15000}).toBe(2);
  expect((await state()).boss.phase).toBe(2);
  await expect.poll(async()=>(await state()).boss.act,{timeout:5000}).toBe(210);
  await page.evaluate(()=>caveStory._web_test_boss_life(150));
  await expect.poll(async()=>(await state()).boss.life,{timeout:5000}).toBeGreaterThan(150);
  await page.evaluate(()=>{caveStory._web_test_boss_part(5,0);caveStory._web_test_boss_part(6,0);});
  await expect.poll(async()=>(await state()).boss.act,{timeout:10000}).toBe(220);
}

const CUTSCENE=['rage','look','transform','transformed','break','crack','crack','out'];

test('hidden level: boots into stage 95, reaches lost+found and back, the enderman goes through its phases, and its pearl ends the fight',async({page})=>{
  test.skip(!process.env.QA_URL,'Build with --test-hooks --output build/qa and set QA_URL to its server.');
  const errors=[];
  // The cutscene as the website scripts it, without the talking.
  const state=await boot(page,errors,'<KEY<WAI0010<BOA0320<WAI0020<BOA0330<WAI0200<BOA0340<WAI0250<FRE<END');
  let s=await state();
  expect(s.me.x>>13).toBe(4);
  expect(await page.evaluate(()=>caveStory._web_test_count(361))).toBe(1);
  // A gateway's event takes you to lost+found (stage 96) and its own brings you back.
  await page.evaluate(()=>caveStory._web_test_event(600));
  await expect.poll(async()=>(await state()).me.stage,{timeout:5000}).toBe(96);
  expect((await state()).me.x>>13).toBe(5);
  await page.evaluate(()=>caveStory._web_test_event(101));
  await expect.poll(async()=>(await state()).me.stage,{timeout:5000}).toBe(95);
  await toStunned(page,state);
  // The last phase opens with a cutscene you can't move in: it looks out of
  // the screen, transforms back to full life, screams until the screen
  // cracks and climbs out. The website draws the parts outside the game from
  // the events the engine sends it (Module.onFlint).
  await page.evaluate(()=>{flint.length=0;});
  await page.evaluate(()=>caveStory._web_test_boss_life(100));
  await expect.poll(async()=>(await state()).boss.phase,{timeout:15000}).toBe(3);
  await expect.poll(()=>page.evaluate(()=>flint[0]),{timeout:5000}).toBe('rage');
  expect((await state()).me.flags&2).toBe(0);
  // It climbs out through the middle of the top of the screen (on the
  // website, it's on top of the game's window there), wherever it was.
  await page.evaluate(()=>{window.leaving=[];const f=()=>{const b=JSON.parse(caveStory.UTF8ToString(caveStory._web_test_boss()));if(b.act===301)leaving.push(b.x);if(!flint.includes('out'))requestAnimationFrame(f);};f();});
  await expect.poll(()=>page.evaluate(()=>flint.slice(0,8).join(' ')),{timeout:30000}).toBe(CUTSCENE.join(' '));
  const leaving=await page.evaluate(()=>leaving);
  expect(leaving.length).toBeGreaterThan(0);
  expect(Math.abs(leaving[leaving.length-1]-160*0x200)).toBeLessThan(20*0x200);
  s=await state();
  expect([s.boss.life,s.boss.rage]).toEqual([300,1]);
  await expect.poll(async()=>(await state()).me.flags&2,{timeout:10000}).toBe(2);
  // (Nothing of its own while these are thrown in.)
  await page.evaluate(()=>caveStory._web_test_throw(3,1,0));
  // Off the site it also throws visitors' cursors in, which turn up as stray
  // cursors, and webring buttons (NPC 370) that fly straight across the room.
  const cursors=await page.evaluate(()=>caveStory._web_test_count(362));
  await page.evaluate(()=>caveStory._web_test_throw(1,160,0));
  await expect.poll(()=>page.evaluate(()=>flint.includes('cursor')),{timeout:2000}).toBe(true);
  await expect.poll(()=>page.evaluate(()=>caveStory._web_test_count(362)),{timeout:5000}).toBe(cursors+1);
  await page.evaluate(()=>caveStory._web_test_throw(2,2,0));
  await expect.poll(()=>page.evaluate(()=>flint.includes('button')),{timeout:2000}).toBe(true);
  await expect.poll(()=>page.evaluate(()=>caveStory._web_test_count(370)),{timeout:5000}).toBe(1);
  const button=()=>page.evaluate(()=>JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(370))));
  const from=(await button()).x;
  await expect.poll(async()=>(await button()).x,{timeout:2000}).toBeGreaterThan(from+0x2000);
  // A piece that breaks into three, and a stack of three buttons from the
  // right with the middle one high, frame by frame with the game paused: one
  // word for each (what, how many, and for the buttons which are high), then
  // "thrown" as each goes in, shortly before it's in the room.
  await expect.poll(()=>page.evaluate(()=>caveStory._web_test_count(370)),{timeout:5000}).toBe(0);
  const stepped=(what,a,b,code,count)=>page.evaluate(([what,a,b,code,count])=>{
    caveStory._web_pause(1);
    said.length=0;
    caveStory._web_test_throw(what,a,b);
    const seen=[];
    for(let f=0;f<200;f++){
      const n=said.length;
      caveStory._web_test_step(1);
      for(const w of said.slice(n))seen.push([w[0],w[1],f]);
      if(caveStory._web_test_count(code)>=count){seen.push(['landed',count,f]);break;}
    }
    caveStory._web_pause(0);
    return seen;
  },[what,a,b,code,count]);
  const piece=await stepped(0,160,3,369,3);
  expect(piece.map(w=>w[0])).toEqual(['throw','thrown','landed']);
  expect(piece[0][1]>>4).toBe(3);
  expect([piece[1][2]-piece[0][2],piece[2][2]-piece[1][2]]).toEqual([85,22]);
  // (The buttons 32 frames apart.)
  const stack=await stepped(2,0,(3<<4)|2,370,3);
  expect(stack).toEqual([['button',0|3<<4|2<<8,0],['thrown',2,85],['thrown',2,117],['thrown',2,149],['landed',3,171]]);
  // What lands looks like what it took: the page gives each thing on its way
  // in a picture (its pixels at twice the game's resolution, a cell for it,
  // and its size), and a piece's pieces are slices of it. A piece that falls
  // in with its hand or the tipping is told to the page too ("fall"), and
  // bits of the site that come down from the ceiling use pictures it has
  // ready ("debris" says which). With no picture, or one whose pixels don't
  // add up, they're drawn by kind from the Void sheet. A pictured piece
  // breaks into bits of its picture; a pictured button goes in by its
  // height, a low one's bottom on the floor and a high one's 25 over it.
  const pictures=await page.evaluate(()=>{
    const SITE=39,VOID=21;
    const find=(code)=>JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(code)));
    const step=(until)=>{for(let f=0;f<300&&!until();f++)caveStory._web_test_step(1);};
    const look=(o)=>o&&[o.surf===SITE?'site':o.surf===VOID?'void':o.surf,o.w,o.h];
    const picture=(what,index,slot,w,h,bytes=w*h*12)=>{caveStory.FS.writeFile('/data/Npc/NpcSite.rgb',new Uint8Array(bytes).fill(200));caveStory._web_site_picture(what,index,slot,w|h<<8);};
    caveStory._web_pause(1);
    step(()=>caveStory._web_test_count(369)===0&&caveStory._web_test_count(370)===0);
    const out={};
    said.length=0;
    caveStory._web_test_throw(0,160,3);
    caveStory._web_test_step(1);
    picture(0,said.find(w=>w[0]==='throw')[1]>>8,5,30,18);
    step(()=>caveStory._web_test_count(369)===3);
    out.piece=look(find(369));
    step(()=>caveStory._web_test_count(369)===0);
    // A big picture comes down from the ceiling (it isn't caught in it) and
    // breaks on the floor.
    said.length=0;
    caveStory._web_test_throw(0,160,1);
    caveStory._web_test_step(1);
    picture(0,said.find(w=>w[0]==='throw')[1]>>8,13,64,40);
    step(()=>caveStory._web_test_count(369)===1);
    out.big=look(find(369));
    const from=find(369).sy;
    let to=from;
    step(()=>{const o=find(369);if(o)to=Math.max(to,o.sy);return caveStory._web_test_count(369)===0;});
    out.dropped=to-from;
    out.broke=caveStory._web_test_count(369);
    // (Among whatever other bits are flying about.)
    out.bits=null;
    for(let n=0;n<60&&!out.bits;n++){const o=JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(368,n)));if(!o)break;if(o.surf===SITE)out.bits=look(o);}
    // Separate pieces, a picture each, whole and side by side.
    said.length=0;
    caveStory._web_test_throw(0,160,3);
    caveStory._web_test_step(1);
    const apart=said.find(w=>w[0]==='throw')[1]>>8;
    picture(0,apart,10,20,10);
    picture(0,apart|1<<4,11,30,12);
    picture(0,apart|2<<4,12,24,8);
    step(()=>caveStory._web_test_count(369)===3);
    const parts=[0,1,2].map(n=>JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(369,n))));
    out.apart=parts.map(look);
    out.gaps=[parts[1].x-parts[0].x,parts[2].x-parts[1].x].map(d=>Math.round(d/0x200));
    step(()=>caveStory._web_test_count(369)===0);
    // (Where each button is as it comes in.)
    const arrivals=(count)=>{const ys=[];for(let f=0;f<300&&ys.length<count;f++){caveStory._web_test_step(1);if(caveStory._web_test_count(370)>ys.length)ys.push(JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(370,ys.length))));}return ys;};
    caveStory._web_test_throw(2,0,1<<4);
    const plain=arrivals(1)[0];
    step(()=>caveStory._web_test_count(370)===0);
    caveStory._web_test_throw(2,0,2<<4|2);
    caveStory._web_test_step(1);
    picture(1,0,6,61,23);
    picture(1,1,14,61,23);
    const both=arrivals(2);
    out.button=look(both[0]);
    out.low=Math.round((both[0].y+0x200*23/2-(plain.y+0x200*8))/0x200);
    out.high=Math.round((both[0].y-both[1].y)/0x200);
    step(()=>caveStory._web_test_count(370)===0);
    said.length=0;
    caveStory._web_test_throw(4,100,0);
    const fall=said.find(w=>w[0]==='fall');
    out.fall=fall&&fall[0];
    picture(0,fall[1]>>8,7,20,12);
    step(()=>caveStory._web_test_count(369)===1);
    out.fell=look(find(369));
    step(()=>caveStory._web_test_count(369)===0);
    said.length=0;
    picture(2,3,8,16,16);
    caveStory._web_test_throw(5,200,0);
    out.debris=[said.filter(w=>w[0]==='debris').map(w=>w[1]),look(find(369))];
    step(()=>caveStory._web_test_count(369)===0);
    said.length=0;
    caveStory._web_test_throw(5,200,0);
    out.plain=[said.filter(w=>w[0]==='debris').length,look(find(369))];
    step(()=>caveStory._web_test_count(369)===0);
    said.length=0;
    caveStory._web_test_throw(0,160,1);
    caveStory._web_test_step(1);
    picture(0,said.find(w=>w[0]==='throw')[1]>>8,9,30,18,30*18*12-1);
    step(()=>caveStory._web_test_count(369)===1);
    out.unpictured=look(find(369))[0];
    caveStory._web_pause(0);
    return out;
  });
  expect(pictures.gaps.every(d=>d>=20&&d<=40)).toBe(true);
  delete pictures.gaps;
  // (Caught in the ceiling, it'd break within a few pixels.)
  expect(pictures.dropped).toBeGreaterThan(20);
  delete pictures.dropped;
  expect(pictures).toEqual({piece:['site',10,18],big:['site',64,40],broke:0,bits:['site',4,4],apart:[['site',20,10],['site',30,12],['site',24,8]],button:['site',61,23],low:0,high:25,fall:'fall',fell:['site',20,12],debris:[[3],['site',16,16]],plain:[0,['void',expect.any(Number),expect.any(Number)]],unpictured:'void'});
  await page.evaluate(()=>caveStory._web_test_throw(3,0,0));
  // Out there it throws pieces of the page in, reaches in or tips the room.
  // Staring at it (the page's cursor resting on it) makes it reach for you;
  // shooting its hand once it's slammed down hurts it.
  await expect.poll(async()=>{
    await page.evaluate(()=>caveStory._web_fight_stare());
    return page.evaluate(()=>flint.includes('stared'));
  },{timeout:20000,intervals:[250]}).toBe(true);
  await expect.poll(async()=>(await state()).boss.hand.damage,{timeout:5000,intervals:[50]}).toBe(2);
  const hold=async(key,ms)=>{await page.keyboard.down(key);await page.waitForTimeout(ms);await page.keyboard.up(key);};
  s=await state();
  const life=s.boss.life;
  await hold(s.boss.hand.x<s.me.x?'ArrowLeft':'ArrowRight',20);
  await expect.poll(async()=>{await hold('KeyX',60);return (await state()).boss.life;},{timeout:3000,intervals:[50]}).toBeLessThan(life);
  await expect.poll(()=>page.evaluate(()=>flint.includes('hurt')),{timeout:2000}).toBe(true);
  // One hit left. It drops back into the room to fight: get in range, face it and shoot.
  await page.evaluate(()=>caveStory._web_test_boss_life(1));
  await expect.poll(()=>page.evaluate(()=>flint.includes('in')),{timeout:60000}).toBe(true);
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
  // It comes apart and drops its pearl. Move it onto its perch (row 5's
  // platform) while it's dying: the pearl must fall past the perch and the
  // low platforms to the floor (row 11 stands on row 12). Walking into the
  // pearl runs #1100. Wait across the room while it falls.
  expect(s.boss.act).toBeGreaterThanOrEqual(500);
  // The death script sets its act; it dies (and tells the page) on its next tick.
  await expect.poll(()=>page.evaluate(()=>flint.includes('die')),{timeout:3000}).toBe(true);
  await page.evaluate(()=>caveStory._web_test_boss_move(10,3));
  await page.evaluate(()=>caveStory._web_test_warp(19,11));
  await expect.poll(()=>page.evaluate(()=>caveStory._web_test_count(365)),{timeout:10000}).toBe(1);
  await expect.poll(async()=>{
    const pearl=await page.evaluate(()=>JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(365))));
    return pearl&&Math.round(pearl.y/0x2000);
  },{timeout:5000}).toBe(11);
  await expect.poll(async()=>{
    s=await state();
    if(!s.won){
      const pearl=await page.evaluate(()=>JSON.parse(caveStory.UTF8ToString(caveStory._web_test_find(365))));
      if(pearl)await hold(pearl.x<s.me.x?'ArrowLeft':'ArrowRight',Math.abs(pearl.x-s.me.x)>32*0x200?250:60);
    }
    return s.won;
  },{timeout:30000,intervals:[100]}).toBe(1);
  // Falling out of the map (a maxed Machine Gun can fly you over its walls)
  // is a death, "You were never seen again...", and the retry starts over.
  await expect.poll(async()=>{s=await state();if(s.me.flags&4)await hold('KeyZ',80);return (s.me.flags&2)&&!(s.me.flags&4)?1:0;},{timeout:20000,intervals:[300]}).toBe(1);
  await page.evaluate(()=>caveStory._web_test_warp(10,30));
  await expect.poll(async()=>(await state()).me.flags&4,{timeout:5000}).toBe(4);
  await expect.poll(async()=>{
    s=await state();
    if(s.me.flags&4)await hold('KeyZ',80);
    return s.me.stage===95&&(s.me.x>>13)===4&&(s.me.flags&2)&&s.me.life>0?1:0;
  },{timeout:40000,intervals:[400]}).toBe(1);
  // The retry tells the page to put itself back together, and the enderman
  // is back in its first form.
  expect(await page.evaluate(()=>flint.includes('reset'))).toBe(true);
  expect((await state()).boss.rage).toBe(0);
  expect(errors).toEqual([]);
});

test('hidden level: without a script for it, the last phase\'s cutscene plays through by itself',async({page})=>{
  test.skip(!process.env.QA_URL,'Build with --test-hooks --output build/qa and set QA_URL to its server.');
  const errors=[];
  const state=await boot(page,errors,'<END');
  await toStunned(page,state);
  await page.evaluate(()=>{flint.length=0;});
  await page.evaluate(()=>caveStory._web_test_boss_life(100));
  await expect.poll(()=>page.evaluate(()=>flint.slice(0,8).join(' ')),{timeout:30000}).toBe(CUTSCENE.join(' '));
  const s=await state();
  expect([s.boss.life,s.boss.rage,s.boss.phase]).toEqual([300,1,3]);
  expect(errors).toEqual([]);
});
