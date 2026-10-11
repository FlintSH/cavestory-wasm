'use strict';
const $ = id => document.getElementById(id);
const canvas = $('canvas');
const gameRoot = new URL('.', document.currentScript.src);
let game, running = false, paused = false, syncQueue = Promise.resolve();
try { $('language').value = localStorage.getItem('cavestory-language') || 'en'; } catch {}
const message = text => { $('save-status').textContent = text; };
function sync(module, populate = false) {
  return new Promise((resolve, reject) => module.FS.syncfs(populate, error => error ? reject(error) : resolve()));
}
function persist(module) {
  // Serialize writes: concurrent IDBFS syncs can overwrite a newer save.
  const operation = syncQueue.catch(() => {}).then(() => sync(module));
  syncQueue = operation;
  operation.then(() => message('Saved to this browser.'), () => message('Storage unavailable. Export your save to keep it.'));
  return operation;
}
$('play').addEventListener('click', async () => {
  $('play').disabled = true;
  $('language').disabled = true;
  const lang = $('language').value;
  $('status').textContent = 'Loading the full game…';
  try {
    try { localStorage.setItem('cavestory-language', lang); } catch {}
    await new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = new URL(`cave-${lang}.js`, gameRoot).href; script.onload = resolve;
      script.onerror = () => reject(new Error('Game download failed. Check your connection and reload.'));
      document.head.append(script);
    });
    game = await createCaveStory({
      canvas, noInitialRun: true,
      locateFile: file => new URL(file, gameRoot).href,
      print: (...args) => console.log(...args),
      printErr: (...args) => console.error(...args),
      onAbort: reason => { $('cover').hidden = false; $('status').textContent = 'Game stopped: '+reason; },
      setStatus: text => { if (text) $('status').textContent = text; },
      persistSave() { return persist(this); },
      onGameExit() {
        running = false;
        $('cover').hidden = false;
        $('status').textContent = 'Game closed. Reload to play again.';
        $('play').textContent = 'Play again';
        $('play').disabled = false;
        $('play').onclick = () => location.reload();
      },
      preRun: [module => {
        module.FS.mkdir('/offline');
        module.FS.mount(module.IDBFS, {}, '/offline');
        module.addRunDependency('restore-save');
        sync(module, true).catch(() => message('Storage unavailable. Export saves to keep your progress.'))
          .finally(() => module.removeRunDependency('restore-save'));
      }],
    });
    // Exposed for save tools and automated browser verification.
    window.caveStory = game;
    game.callMain([]);
    running = true;
    $('cover').hidden = true;
    for (const id of ['pause','fullscreen','export','import']) $(id).disabled = false;
    canvas.focus();
  } catch (error) {
    $('status').textContent = error.message;
    $('play').textContent = 'Reload to retry';
    $('play').disabled = false;
    $('play').onclick = () => location.reload();
  }
});
function setPaused(value) {
  if (!running || paused === value) return;
  paused = value; game._web_pause(+value);
  $('paused').hidden = !value;
  $('pause').textContent = value ? 'Resume' : 'Pause';
  if (!value) canvas.focus();
}
$('pause').addEventListener('click', () => setPaused(!paused));
document.addEventListener('visibilitychange', () => { if (document.hidden) setPaused(true); });
$('fullscreen').addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await $('screen').requestFullscreen();
    canvas.focus();
  } catch { message('Fullscreen is unavailable in this browser.'); }
});
$('export').addEventListener('click', async () => {
  try {
    await syncQueue.catch(() => {});
    const bytes = game.FS.readFile('/offline/Profile.dat');
    const url = URL.createObjectURL(new Blob([bytes], {type:'application/octet-stream'}));
    const link = document.createElement('a'); link.href = url; link.download = 'Profile.dat';
    link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
    message('Save exported.');
  } catch { message('No save yet. Use an in-game save point first.'); }
  canvas.focus();
});
$('import').addEventListener('change', async event => {
  const file = event.target.files[0]; if (!file) return;
  const wasPaused = paused; setPaused(true);
  try {
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes.length !== 0x604 || new TextDecoder().decode(bytes.slice(0,8)) !== 'Do041220'
        || new TextDecoder().decode(bytes.slice(0x218,0x21c)) !== 'FLAG') {
      throw new Error('Choose an original Cave Story Profile.dat save (1,540 bytes).');
    }
    const view = new DataView(bytes.buffer);
    if (view.getUint32(8,true) >= 95 || view.getUint16(28,true) > 1000
        || view.getUint32(36,true) >= 8 || view.getUint32(40,true) >= 32) throw new Error('Invalid Cave Story save.');
    await syncQueue.catch(() => {});
    game.FS.writeFile('/offline/Profile.dat', bytes);
    await persist(game);
    // Restart so the title screen reads the imported save rather than stale state.
    location.reload();
  } catch (error) { message(error.message); if (!wasPaused) setPaused(false); }
  event.target.value = '';
});
const heldPointers = new Map();
for (const button of document.querySelectorAll('[data-key]')) {
  button.addEventListener('pointerdown', event => {
    if (!running || paused) return;
    event.preventDefault(); button.setPointerCapture(event.pointerId);
    const key = +button.dataset.key;
    heldPointers.set(event.pointerId,{button,key});
    button.classList.add('held'); game._web_key(key,1);
  });
  for (const type of ['pointerup','pointercancel','lostpointercapture']) button.addEventListener(type,event => {
    const held = heldPointers.get(event.pointerId); if (!held) return;
    heldPointers.delete(event.pointerId);
    if (![...heldPointers.values()].some(value => value.key === held.key)) {
      held.button.classList.remove('held'); game._web_key(held.key,0);
    }
  });
}
canvas.addEventListener('contextmenu', event => event.preventDefault());
window.addEventListener('keydown', event => {
  if (document.activeElement === canvas && ['ArrowUp','ArrowDown','ArrowLeft','ArrowRight',' ','Escape','F1','F2'].includes(event.key)) event.preventDefault();
});
window.addEventListener('pagehide', () => { if (game) persist(game).catch(() => {}); });
