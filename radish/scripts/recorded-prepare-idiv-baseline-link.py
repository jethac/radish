from pathlib import Path
import hashlib,json,shlex,subprocess,shutil
W=Path('/run/media/jethac/Shared/radish');T=W/'core-api-0044-ulrh8m0m';B=T/'build';D=W/'compute11-idiv-isolated-build-0045';D.mkdir()
def pin(p):
 return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def write(p,j):p.write_text(json.dumps(j,indent=2)+'\n')
raw=subprocess.run(['ninja','-C',str(B),'-t','commands','src/freedreno/radish/libvulkan_radish.so'],capture_output=True,text=True,check=True).stdout.splitlines()
links=[shlex.split(x)for x in raw if ' -o src/freedreno/radish/libvulkan_radish.so 'in x];assert len(links)==1
link=links[0];commands=json.loads((B/'compile_commands.json').read_text());cc=[x for x in commands if x['file'].endswith('/nir_lower_idiv.c')];assert len(cc)==1
archive_lines=[x for x in raw if x.startswith('rm -f src/compiler/nir/libnir.a && llvm-ar csrDT ')];assert len(archive_lines)==1
archive=shlex.split(archive_lines[0].split(' && ',1)[1]);assert archive[:3]==['llvm-ar','csrDT','src/compiler/nir/libnir.a']
inputs={}
for arg in link:
 p=Path(arg);p=p if p.is_absolute() else B/p
 if p.is_file():inputs[arg]=pin(p)
assert inputs['src/compiler/nir/libnir.a']['sha256']==pin(B/'src/compiler/nir/libnir.a')['sha256']
write(D/'frozen-commands.json',dict(cwd=str(B),link=link,compile=cc[0],nir_archive=archive,link_inputs=inputs,nir_members=[pin(B/p)for p in archive[3:]]))
baseline=link.copy();baseline[baseline.index('-o')+1]=str(D/'libvulkan_radish.baseline.so')
with (D/'baseline-link.log').open('x')as f:
 r=subprocess.run(baseline,cwd=B,stdout=f,stderr=subprocess.STDOUT)
write(D/'baseline-link-status.json',dict(argv=baseline,cwd=str(B),returncode=r.returncode));assert r.returncode==0
actual=pin(D/'libvulkan_radish.baseline.so');assert actual['sha256']=='f222ff480f6c9ef3785c446661f12ccacbc91f119b0e7d405347b949b4dc325d',actual
for arg,old in inputs.items():assert pin(Path(old['path']))==old
src=T/'mesa/src/compiler/nir/nir_lower_idiv.c';shutil.copy2(src,D/'nir_lower_idiv.baseline.c')
write(D/'baseline-result.json',dict(status='PASS isolated baseline relink byte-identical to immutable f222; all original inputs unchanged',driver=actual,source=pin(src),commands=pin(D/'frozen-commands.json'),scope='No patched source or candidate build yet. T/mesa/T/build and original archive remain immutable.'))
print('PASS isolated baseline relink',actual['sha256'],flush=True)
