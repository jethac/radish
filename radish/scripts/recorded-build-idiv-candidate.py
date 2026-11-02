from pathlib import Path
import hashlib,json,shlex,subprocess,shutil,struct
W=Path('/run/media/jethac/Shared/radish');D=W/'compute11-idiv-isolated-build-0045';R=W/'compute11-idiv-reciprocal-fix-0045';F=json.loads((D/'frozen-commands.json').read_text());B=Path(F['cwd'])
def pin(p):return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def write(p,j):p.write_text(json.dumps(j,indent=2)+'\n')
patch=R/'idiv-reciprocal.patch';assert pin(patch)['sha256']=='292b4e24aae6092e161c2fb040d2671c18655ea94e97d1f99e18c844348efe20'
src=D/'source/src/compiler/nir/nir_lower_idiv.c';src.parent.mkdir(parents=True);shutil.copy2(D/'nir_lower_idiv.baseline.c',src)
baseline=pin(src)
with (D/'patch.log').open('x')as f:r=subprocess.run(['patch','--batch','-p1','-i',str(patch)],cwd=D/'source',stdout=f,stderr=subprocess.STDOUT)
assert r.returncode==0
obj=D/'nir_lower_idiv.c.o';dep=D/'nir_lower_idiv.c.o.d';cc=shlex.split(F['compile']['command']);assert cc[-1]==F['compile']['file'];cc[-1]=str(src)
for flag,path in [('-o',obj),('-MF',dep),('-MQ',obj)]:cc[cc.index(flag)+1]=str(path)
with (D/'compile.log').open('x')as f:r=subprocess.run(cc,cwd=B,stdout=f,stderr=subprocess.STDOUT)
write(D/'compile-status.json',dict(argv=cc,cwd=str(B),returncode=r.returncode));assert r.returncode==0
archive=D/'libnir.idiv.a';ar=F['nir_archive'];memberpaths=[B/p for p in ar[3:]];target=B/F['compile']['output'];assert memberpaths.count(target)==1
patched=[obj if p==target else p for p in memberpaths]
arcmd=['llvm-ar','csrD',str(archive)]+list(map(str,patched))
with (D/'archive.log').open('x')as f:r=subprocess.run(arcmd,cwd=B,stdout=f,stderr=subprocess.STDOUT)
assert r.returncode==0
# Verify normal archive members byte-for-byte against the original object inventory.
data=archive.read_bytes();assert data[:8]==b'!<arch>\n';offset=8;strings=b'';members=[]
while offset<len(data):
 h=data[offset:offset+60];assert h[58:60]==b'`\n';size=int(h[48:58]);name=h[:16].decode().strip();body=data[offset+60:offset+60+size];offset+=60+size+(size%2)
 if name=='//':strings=body;continue
 if name in ('/','/SYM64/'):continue
 if name.startswith('/')and name[1:].isdigit():name=strings[int(name[1:]):].split(b'/\n',1)[0].decode()
 else:name=name.rstrip('/')
 members.append((name,hashlib.sha256(body).hexdigest(),len(body)))
assert len(members)==len(memberpaths)
changes=[]
for (name,sha,size),old,new in zip(members,memberpaths,patched):
 assert name==new.name and sha==pin(new)['sha256'] and size==new.stat().st_size
 if sha!=pin(old)['sha256']:changes.append(name)
assert changes==['nir_lower_idiv.c.o']
link=F['link'].copy();link[link.index('-o')+1]=str(D/'libvulkan_radish.idiv.so');assert link.count('src/compiler/nir/libnir.a')==1;link[link.index('src/compiler/nir/libnir.a')]=str(archive)
with (D/'candidate-link.log').open('x')as f:r=subprocess.run(link,cwd=B,stdout=f,stderr=subprocess.STDOUT)
assert r.returncode==0
for old in F['link_inputs'].values():assert pin(Path(old['path']))==old
for old in F['nir_members']:assert pin(Path(old['path']))==old
dependencies=[]
for raw in shlex.split(dep.read_text().split(':',1)[1].replace('\\\n',' ')):
 p=Path(raw);p=p if p.is_absolute()else B/p;dependencies.append(pin(p.resolve()))
out=D/'libvulkan_radish.idiv.so';elf=out.read_bytes();assert struct.unpack_from('<H',elf,18)[0]==183
write(D/'candidate-result.json',dict(status='PASS isolated shared-NIR candidate host build; no device qualification',driver=pin(out),baseline=json.loads((D/'baseline-result.json').read_text()),patch=pin(patch),baseline_source=baseline,candidate_source=pin(src),object=pin(obj),archive=pin(archive),archive_members=len(members),changed_archive_members=changes,commands=dict(compile=cc,archive=arcmd,link=link,cwd=str(B)),dependencies=dependencies,scope='Only one NIR object changes; every original link input and archive member file remains immutable. Candidate requires independent source/host/native and original numerical gates.'))
print('PASS isolated NIR candidate',pin(out)['sha256'],'members',len(members),'changed',changes,flush=True)
