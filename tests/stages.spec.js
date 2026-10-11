const {test,expect}=require('@playwright/test');
test('all 95 original stages, NPC/boss ticks and credits resources',async({page})=>{
  test.skip(!process.env.QA_URL,'Build with --test-hooks --output build/qa and set QA_URL to its server.');
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(process.env.QA_URL);await page.click('#play');await expect(page.locator('#cover')).toBeHidden();
  // Allow the intro to load base surfaces, then exercise the real stage loader independently.
  await page.waitForTimeout(2000);await page.click('#pause');
  const results=await page.evaluate(()=>{
    const results=[];
    for(let stage=0;stage<95;stage++) {
      const loaded=caveStory._web_test_stage(stage);
      caveStory._web_test_step(100);
      const stats=JSON.parse(caveStory.UTF8ToString(caveStory._web_test_stats()));
      results.push({stage,loaded,...stats});
    }
    return results;
  });
  for(const result of results) {
    expect(result.loaded,`stage ${result.stage}`).toBe(1);
    expect(result.mapWidth,`stage ${result.stage} width`).toBeGreaterThan(0);
    expect(result.mapHeight,`stage ${result.stage} height`).toBeGreaterThan(0);
  }
  console.log('Loaded and simulated every original stage:',results.length);
  const credits=await page.evaluate(()=>{
    const loaded=caveStory._web_test_credits();
    caveStory._web_test_credit_step(25000);
    for(const illustration of [1,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18])caveStory._web_test_illustration(illustration);
    return {loaded,...JSON.parse(caveStory.UTF8ToString(caveStory._web_test_stats()))};
  });
  expect(credits.loaded).toBe(1);expect(credits.creditOffset).toBeGreaterThan(0);
  expect(errors).toEqual([]);
});
