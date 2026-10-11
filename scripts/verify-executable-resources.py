from pathlib import Path
import struct,re,json,hashlib
import argparse
parser=argparse.ArgumentParser(description='Check CSE2 embedded assets against the original freeware PE executables.')
parser.add_argument('--english',type=Path,required=True)
parser.add_argument('--japanese',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
def read_resources(path):
 b=path.read_bytes();pe=struct.unpack_from('<I',b,0x3c)[0];n=struct.unpack_from('<H',b,pe+6)[0];optsize=struct.unpack_from('<H',b,pe+20)[0];opt=pe+24
 sections=[struct.unpack_from('<IIII',b,opt+optsize+i*40+8) for i in range(n)]
 def rva(addr):
  for size,base,rawsize,raw in sections:
   if base<=addr<base+max(size,rawsize):return raw+addr-base
  raise ValueError(hex(addr))
 resource_rva=struct.unpack_from('<I',b,opt+96+2*8)[0];base=rva(resource_rva);out={}
 def walk(off,keys):
  count=sum(struct.unpack_from('<HH',b,base+off+12))
  for i in range(count):
   key,value=struct.unpack_from('<II',b,base+off+16+i*8)
   if key&0x80000000:
    ko=base+(key&0x7fffffff);length=struct.unpack_from('<H',b,ko)[0];key=b[ko+2:ko+2+length*2].decode('utf-16le')
   path=keys+[key]
   if value&0x80000000:walk(value&0x7fffffff,path)
   else:
    addr,size=struct.unpack_from('<II',b,base+value);out[tuple(path)]=b[rva(addr):rva(addr)+size]
 walk(0,[]);return out
src=(root/'src/Resource.cpp').read_text()
entries=re.findall(r'\{"(ORG|WAVE|BITMAP)", "([^\"]+)", r(\w+),',src)
report={}
for lang,path in [('en',args.english),('jp',args.japanese)]:
 res=read_resources(Path(path));verified=[];unmatched=[]
 for type_,name,symbol in entries:
  if symbol=='pixel_jp' and lang=='en' or symbol=='pixel' and lang=='jp':continue
  keys=[k for k in res if k[0]==(2 if type_=='BITMAP' else type_) and str(k[1]).upper()==name]
  files=list((root/'assets/resources'/type_).glob(symbol+'.*'))
  if not files:raise Exception(symbol)
  data=files[0].read_bytes();data=data[14:] if type_=='BITMAP' else data
  if not keys:unmatched.append([type_,name,'resource not found']);continue
  if data!=res[keys[0]]:unmatched.append([type_,name,'content differs']);continue
  verified.append({'type':type_,'name':name,'sha256':hashlib.sha256(data).hexdigest()})
 print(lang,'matched',len(verified),'differences',unmatched)
 assert len(verified)==61 and not unmatched, f'{lang}: resources differ from the original'
 report[lang]={'executable_sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest(),'verified':verified,'differences':unmatched}
(root/'docs/embedded-resource-verification.json').write_text(json.dumps(report,indent=2)+'\n')
