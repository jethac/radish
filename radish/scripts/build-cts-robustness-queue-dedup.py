from pathlib import Path
import json,hashlib,subprocess,shlex,shutil,struct,os
W=Path(os.environ.get('RADISH_WORK','/run/media/jethac/Shared/radish')).resolve();B=W/'build-cts';S=Path(__file__).resolve().parents[1]/'tests/cts-robustness-queue-dedup';D=W/'compute11-robustness-queue-dedup-build-0045';D.mkdir();release=json.loads((S/'release.json').read_text());O=B/'external/vulkancts/modules/vulkan/deqp-vk';target='external/vulkancts/modules/vulkan/robustness/CMakeFiles/deqp-vk-robustness.dir/vktRobustnessUtil.cpp.o';librel='external/vulkancts/modules/vulkan/robustness/libdeqp-vk-robustness.a';L=B/librel

def pin(p):return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def run(argv,name):
 (D/(name+'-command.json')).write_text(json.dumps(dict(argv=argv,cwd=str(B)),indent=2)+'\n')
 with(D/(name+'.log')).open('x')as f:r=subprocess.run(argv,cwd=B,stdout=f,stderr=subprocess.STDOUT)
 (D/(name+'-status.json')).write_text(json.dumps(dict(returncode=r.returncode))+'\n');assert r.returncode==0,name
before=pin(O);assert before['sha256']=='0e715f9c53ec1dfd742d93a7a45740cbf4a8caa9193fe761e71dd6347c2a6660'
orig=W/'downloads/vk-gl-cts/external/vulkancts/modules/vulkan/robustness/vktRobustnessUtil.cpp';assert pin(orig)['sha256']==release['preimage']['sha256'];cand=S/'candidate/vktRobustnessUtil.cpp';assert pin(cand)['sha256']==release['candidate']['sha256'];shutil.copy2(cand,D/cand.name)
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands','external/vulkancts/modules/vulkan/deqp-vk'],text=True).splitlines();link=shlex.split(commands[-1]);assert link[:2]==[':','&&']and link[-2:]==['&&',':'];link=link[2:-2];assert link[link.index('-o')+1]=='external/vulkancts/modules/vulkan/deqp-vk';assert librel in link
inputs={str((B/x).resolve()):pin((B/x).resolve())for x in link if x.endswith(('.a','.o','.so')) and(B/x).is_file()};(D/'original-link-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n');(D/'original-link-command.json').write_text(json.dumps(link,indent=2)+'\n')
def link_for(out,lib):
 a=link.copy();a[a.index('-o')+1]=str(out);a=[('--dependency-file='+str(D/(out.name+'.d')))if x.startswith('--dependency-file=')else x for x in a];a=[str(lib)if x==librel else x for x in a];return a
run(link_for(D/'deqp-vk-baseline',L),'baseline-link');base=pin(D/'deqp-vk-baseline');assert base['sha256']==before['sha256'],'retained link inputs do not reproduce exact original binary';print('PASS isolated baseline relink exact0e715',flush=True)
raw=subprocess.check_output(['ninja','-C',str(B),'-t','commands',target],text=True).splitlines()[-1];a=shlex.split(raw);assert a[-2:]==['-c',str(orig)];a[-1]=str(D/cand.name);obj=D/'vktRobustnessUtil.cpp.o';a[a.index('-o')+1]=str(obj);a[a.index('-MF')+1]=str(D/'vktRobustnessUtil.cpp.o.d');a[a.index('-MT')+1]=str(obj);a[1:1]=['-iquote',str(orig.parent)];run(a,'candidate-compile');assert struct.unpack_from('<H',obj.read_bytes(),18)[0]==183
A=D/'libdeqp-vk-robustness.a';shutil.copy2(L,A);run(['/usr/bin/llvm-ar','r',str(A),str(obj)],'archive-replace');run(['/usr/bin/llvm-ranlib',str(A)],'archive-index')
names=subprocess.check_output(['/usr/bin/llvm-ar','t',str(L)],text=True).splitlines();assert names==subprocess.check_output(['/usr/bin/llvm-ar','t',str(A)],text=True).splitlines();changed=[]
for n in names:
 x=subprocess.check_output(['/usr/bin/llvm-ar','p',str(L),n]);y=subprocess.check_output(['/usr/bin/llvm-ar','p',str(A),n]);
 if x!=y:changed.append(n)
assert changed==['vktRobustnessUtil.cpp.o'],changed
run(link_for(D/'deqp-vk-queue-dedup',A),'candidate-link');assert struct.unpack_from('<H',(D/'deqp-vk-queue-dedup').read_bytes(),18)[0]==183
for v in inputs.values():assert pin(Path(v['path']))==v
assert pin(O)==before and pin(orig)['sha256']==release['preimage']['sha256']
dep=(D/'vktRobustnessUtil.cpp.o.d').read_text().replace('\\\n','');deps=[Path(x)if Path(x).is_absolute()else B/x for x in shlex.split(dep.split(':',1)[1])];headers={str(p):pin(p)for p in deps if p.is_file()};(D/'dependencies.json').write_text(json.dumps(headers,indent=2)+'\n')
result=dict(status='PASS isolated AArch64 modified-harness host build; no device qualification',original_binary=before,baseline_relink=base,modified_binary=pin(D/'deqp-vk-queue-dedup'),candidate_source=pin(D/cand.name),patch=pin(S/'queue-dedup.patch'),old_archive=pin(L),new_archive=pin(A),changed_archive_members=changed,unchanged_members=len(names)-1,original_link_inputs=pin(D/'original-link-inputs.json'),dependencies=pin(D/'dependencies.json'),scope='Exact original0e715 baseline reproduced from retained link inputs; only reviewed queue-dedup object replaced in isolated archive. All original archives/source/executable remain unchanged. Shaders/oracles/data unmodified. This is a modified CTS harness and its outcomes must not enter originalCTS counts. Driverf222/e699 unchanged.');(D/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS modified robustness harness host build',result['modified_binary'],flush=True)
