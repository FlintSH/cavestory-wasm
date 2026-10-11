const { defineConfig } = require('@playwright/test');
module.exports = defineConfig({
  testDir: './tests', timeout: 120000, workers: 1, retries: 0,
  reporter: [['list'], ['html', {open:'never'}]],
  use: {
    baseURL: process.env.TEST_URL || 'http://localhost:8000',
    viewport: {width:1050,height:1000},
    launchOptions: {
      ...(process.env.CHROME_BIN ? {executablePath:process.env.CHROME_BIN} : {}),
      args:['--no-sandbox','--autoplay-policy=no-user-gesture-required','--enable-unsafe-swiftshader'],
    },
    screenshot:'only-on-failure', video:'on', trace:'retain-on-failure',
  },
  webServer: process.env.TEST_URL ? undefined : [{
    command:'python3 -m http.server 8000 --directory dist',
    url:'http://localhost:8000', reuseExistingServer:!process.env.CI,
  }, ...(process.env.QA_URL ? [{
    command:'python3 -m http.server 8001 --directory build/qa',
    url:process.env.QA_URL, reuseExistingServer:!process.env.CI,
  }] : [])],
});
