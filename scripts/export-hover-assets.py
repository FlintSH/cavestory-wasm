#!/usr/bin/env python3
"""Export Quote's sprite sheet and the original menu sound for the website easter egg."""
from pathlib import Path
import struct,zlib,re,subprocess,wave
root=Path(__file__).resolve().parent.parent
out=root/'web'; b=(root/'assets/data_en/MyChar.pbm').read_bytes()
off=struct.unpack_from('<I',b,10)[0];w,h=struct.unpack_from('<ii',b,18);bits=struct.unpack_from('<H',b,28)[0];stride=((w*bits+31)//32)*4
paloff=14+struct.unpack_from('<I',b,14)[0]
palette=[b[paloff+i*4:paloff+i*4+3][::-1] for i in range(1<<bits)] if bits<=8 else []
rows=[]
for y in range(abs(h)):
 row=b[off+(abs(h)-1-y if h>0 else y)*stride:][:stride]
 colors=[palette[(row[x//2]>>(4 if x%2==0 else 0))&15] if bits==4 else palette[row[x]] if bits==8 else row[x*3:x*3+3][::-1] for x in range(w)]
 # Same black color key as the game's renderer. No rescaling or filtering.
 rows.append(b'\0'+b''.join(c+(b'\0' if c==b'\0\0\0' else b'\xff') for c in colors))
def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
(out/'quote.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,abs(h),8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))
# Menu cursor sound = sound #1 = gPtpTable[33], a single PixTone channel.
text=(root/'src/GenericLoad.cpp').read_text();table=text.split('gPtpTable[139] =')[1].split('};')[0]
parameters=[line.strip().rstrip(',') for line in table.splitlines() if line.strip().startswith('{1,')]
pt=parameters[33]
build=root/'build';build.mkdir(exist_ok=True)
rng=(root/'src/Main.cpp').read_text().split('// A replication of MSVC')[1].split('// Framerate stuff')[0]
rng=rng[rng.index('static unsigned'):];
cpp=build/'export-hover-sound.cpp';cpp.write_text('#include "PixTone.h"\n#include <cstdio>\n#include <vector>\n'+rng+'\nint main(){ const PIXTONEPARAMETER parameter = '+pt+'; std::vector<unsigned char> samples(parameter.size,128); if(!MakePixelWaveData(&parameter,samples.data()))return 1; fwrite(samples.data(),1,samples.size(),stdout); }\n')
subprocess.run(['g++','-std=c++11','-O2','-fwrapv','-I'+str(root/'src'),str(cpp),str(root/'src/PixTone.cpp'),'-o',str(build/'export-hover-sound')],check=True)
data=subprocess.check_output([str(build/'export-hover-sound')])
with wave.open(str(out/'cave-hover.wav'),'wb') as wav:
 wav.setnchannels(1);wav.setsampwidth(1);wav.setframerate(22050);wav.writeframes(data)
print('Exported original',w,'x',abs(h),'sprite sheet and',len(data),'menu sound samples')
