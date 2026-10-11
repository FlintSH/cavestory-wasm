const {test,expect}=require('@playwright/test');
const fs=require('node:fs');
const path=require('node:path');
const fixture=path.join(__dirname,'fixtures/Profile.dat');
const state=page=>page.evaluate(()=>JSON.parse(caveStory.UTF8ToString(caveStory._web_state())));
async function play(page){await page.click('#play');await expect(page.locator('#cover')).toBeHidden({timeout:30000});}
async function load(page){
 await page.locator('#canvas').focus();
 for(let i=0;i<40;i++){
  if((await state(page)).stage===13 && ((await state(page)).flags&2))return;
  await page.keyboard.press('z',{delay:80});await page.waitForTimeout(200);
 }
 throw new Error('Save did not load');
}
test('original save import, in-game save point, IndexedDB reload and export',async({page})=>{
 await page.goto('/');await play(page);
 await page.setInputFiles('#import',fixture);
 await expect(page.locator('#play')).toBeEnabled();
 await play(page);await load(page);await page.waitForTimeout(1500);
 const loaded=await state(page);expect(loaded.life).toBe(3);expect(loaded.stage).toBe(13);
 await page.keyboard.press('ArrowDown',{delay:150});
 await page.waitForTimeout(1000);
 // Advance the original save-point prompt, choose Yes, and dismiss its message.
 for(let i=0;i<30;i++){
  await page.keyboard.press('z',{delay:80});await page.waitForTimeout(200);
  const saved=await page.evaluate(()=>{const bytes=caveStory.FS.readFile('/offline/Profile.dat');return !!(bytes[0x21c+Math.floor(431/8)] & (1<<(431%8)));});
  if(saved)break;
 }
 const saved=await page.evaluate(()=>{const b=caveStory.FS.readFile('/offline/Profile.dat');return !!(b[0x21c+Math.floor(431/8)] & (1<<(431%8)));});
 expect(saved,'original <SVP> save point executed').toBe(true);
 await expect(page.locator('#save-status')).toHaveText('Saved to this browser.');
 const downloadPromise=page.waitForEvent('download');await page.click('#export');
 const download=await downloadPromise;expect(download.suggestedFilename()).toBe('Profile.dat');
 const exported=fs.readFileSync(await download.path());expect(exported.length).toBe(1540);expect(exported.subarray(0,8).toString()).toBe('Do041220');
 await page.reload();await play(page);
 expect(await page.evaluate(()=>caveStory.FS.readFile('/offline/Profile.dat').length)).toBe(1540);
 await load(page);expect((await state(page)).stage).toBe(13);
 const before=await page.evaluate(()=>Array.from(caveStory.FS.readFile('/offline/Profile.dat')));
 await page.setInputFiles('#import',{name:'bad.dat',mimeType:'application/octet-stream',buffer:Buffer.from('not a Cave Story save')});
 await expect(page.locator('#save-status')).toContainText('original Cave Story Profile.dat');
 expect(await page.evaluate(()=>Array.from(caveStory.FS.readFile('/offline/Profile.dat')))).toEqual(before);
});
