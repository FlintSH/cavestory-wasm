#!/usr/bin/env python3
"""Build both complete game engines with Emscripten (no native dependencies)."""
import argparse, concurrent.futures, hashlib, json, os, re, shutil, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
parser = argparse.ArgumentParser()
parser.add_argument('--language', choices=['en','jp','both'], default='both')
parser.add_argument('--debug', action='store_true')
parser.add_argument('--test-hooks', action='store_true', help='Include stage/credits test entrypoints; never deploy this build')
parser.add_argument('--output', type=Path, default=Path('dist'))
args = parser.parse_args()
compiler = shutil.which('em++')
if not compiler:
    raise SystemExit('em++ missing. Activate Emscripten 4.0.21: source /path/to/emsdk/emsdk_env.sh')
# Embed the original executable resources, exactly as the upstream bin2h tool did.
for relative in re.findall(r'#include "Resource/([^\"]+)\.h"', Path('src/Resource.cpp').read_text()):
    source = Path('assets/resources') / relative
    target = Path('src/Resource') / (relative + '.h')
    target.parent.mkdir(parents=True, exist_ok=True)
    symbol = 'r' + source.name.split('.')[0]
    data = source.read_bytes()
    content = '#pragma once\nstatic const unsigned char '+symbol+'['+str(len(data))+'] = {\n'
    content += ',\n'.join(','.join(map(str,data[i:i+32])) for i in range(0,len(data),32))+'\n};\n'
    if not target.exists() or target.read_text() != content: target.write_text(content)
output=args.output
output.mkdir(parents=True,exist_ok=True)
for source in Path('web').iterdir():
    if source.is_file(): shutil.copy2(source, output / source.name)
langs = ['en','jp'] if args.language == 'both' else [args.language]
for lang in langs:
    objdir = Path('build') / ('qa-'+lang if args.test_hooks else lang)
    objdir.mkdir(parents=True,exist_ok=True)
    flags = ['-std=c++11', '-O0' if args.debug else '-O2', '-fwrapv', '-fno-strict-aliasing',
             '-sUSE_SDL=2', '-sUSE_FREETYPE=1', '-Isrc']
    if lang == 'jp': flags += ['-DJAPANESE']
    exports=['_main','_web_state','_web_key','_web_pause','_web_fight','_web_flag','_web_fight_stare','_web_site_picture','_web_site_reload']
    if args.test_hooks:
        flags += ['-DWEB_TEST_HOOKS']
        exports += ['_web_test_stage','_web_test_credits','_web_test_illustration','_web_test_load','_web_test_stats','_web_test_step','_web_test_credit_step','_web_test_save','_web_test_boss','_web_test_boss_life','_web_test_boss_phase','_web_test_warp','_web_test_count','_web_test_event','_web_test_boss_part','_web_test_life','_web_test_find','_web_test_exp','_web_test_boss_move','_web_test_throw']
    if args.debug: flags += ['-g', '-sASSERTIONS=2', '-sSAFE_HEAP=1']
    sources = sorted(Path('src').glob('*.cpp'))
    # Warm the SDK ports once before parallel compilation.
    subprocess.run([compiler, *flags, '-c', 'src/ArmsItem.cpp', '-o', str(objdir/'ArmsItem.o')],check=True)
    def compile_one(source):
        obj = objdir / (source.stem+'.o')
        subprocess.run([compiler,*flags,'-c',str(source),'-o',str(obj)],check=True)
        return str(obj)
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 2,8)) as pool:
        objects = list(pool.map(compile_one, sources))
    print('Linking',lang,flush=True)
    subprocess.run([compiler,*flags,*objects,'-o',str(output/('cave-'+lang+'.js')),
        '-sMODULARIZE=1','-sEXPORT_NAME=createCaveStory','-sALLOW_MEMORY_GROWTH=1',
        '-sINITIAL_MEMORY=33554432','-sSTACK_SIZE=1048576','-sENVIRONMENT=web',
        '-sFORCE_FILESYSTEM=1','-sEXIT_RUNTIME=0','-lidbfs.js',
        '-sEXPORTED_RUNTIME_METHODS=["FS","IDBFS","callMain","UTF8ToString"]',
        '-sEXPORTED_FUNCTIONS='+str(exports).replace("'",'"'),
        '--preload-file','assets/data_'+lang+'@/data',
        '--preload-file','assets/emscripten/Config.dat@/Config.dat'],check=True)
(output/'.nojekyll').touch()
source_digest=hashlib.sha256()
for path in sorted([*Path('src').glob('*.cpp'), *Path('src').glob('*.h'), *Path('web').glob('*')]):
    if path.is_file(): source_digest.update(str(path).encode()+b'\0'+path.read_bytes())
manifest={
    'engine':'CSE2', 'upstream_commit':'5c65455e603744fc7eb9eea34ad1b608b641de6a',
    'emscripten':'4.0.21', 'source_tree_sha256':source_digest.hexdigest(),
    'test_hooks':args.test_hooks,
    'files':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(output.iterdir()) if path.is_file() and path.name not in ['version.json','.nojekyll']},
}
(output/'version.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Ready: python3 -m http.server 8000 --directory dist')
