from pathlib import Path
import json,hashlib,subprocess,shutil,os,shlex,concurrent.futures,re
W=Path('/run/media/jethac/Shared/radish');I=W/'optimization-five-asr-integrated-build-0046';B=W/'optimization-five-asr-mv32-build-0046';D=W/'optimization-five-asr-mm128-rms-build-0046';O=W/'optimization-five-queue-asr-0046';M=O/'quant-mm-sequential-tiles';R=O/'rms-runtime-body';D.mkdir(exist_ok=True);h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
mr=json.loads((M/'release.json').read_text());rr=json.loads((R/'release.json').read_text());br=json.loads((B/'result.json').read_text())
for n,p in mr['files'].items():assert h(Path(p['preimage_path']))==p['baseline'] and h(M/'candidate'/n)==p['candidate']
assert h(Path(rr['preimage']['path']))==rr['preimage']['sha256'] and h(Path(rr['candidate']['path']))==rr['candidate']['sha256']
old=I/'transcribe.cpp/ggml/src/ggml-vulkan/vulkan-shaders';shutil.copytree(old,D/'shader-source',dirs_exist_ok=True);shutil.copy2(M/'candidate/ggml/src/ggml-vulkan/vulkan-shaders/mul_mm_khr_quant.comp',D/'shader-source/mul_mm_khr_quant.comp');shutil.copy2(Path(rr['candidate']['path']),D/'shader-source/rms_norm.comp')
gen=I/'build/Release/vulkan-shaders-gen';G=I/'build/transcribe/ggml/src/ggml-vulkan';commands=[];modules={}
for family,shader,baseline,reference,names,unchanged in [('quant','mul_mm_khr_quant.comp',B/'shader-source/mul_mm_khr_quant.comp',B/'candidate/spv',mr['shader_variants']['changed']+mr['shader_variants']['unchanged'],mr['shader_variants']['unchanged']),('rms','rms_norm.comp',old/'rms_norm.comp',G/'vulkan-shaders.spv',['rms_norm_f32','rms_norm_mul_add_f32','rms_norm_mul_rope_f32_f16','rms_norm_mul_rope_f32_f32','rms_norm_set_rows_f32_f16','rms_norm_set_rows_f32_f32'],['rms_norm_mul_add_f32','rms_norm_mul_rope_f32_f16','rms_norm_mul_rope_f32_f32','rms_norm_set_rows_f32_f16','rms_norm_set_rows_f32_f32'])]:
 for tag,src in [('baseline',baseline),('candidate',D/'shader-source'/shader)]:
  out=D/family/tag;out.mkdir(parents=True,exist_ok=True);(out/'spv').mkdir(exist_ok=True);shutil.copy2(G/'ggml-vulkan-shaders.hpp',out/'ggml-vulkan-shaders.hpp');a=[str(gen),'--glslc','/usr/bin/glslc','--source',str(src),'--output-dir',str(out/'spv'),'--target-hpp',str(out/'ggml-vulkan-shaders.hpp'),'--target-cpp',str(out/(shader+'.cpp'))];commands.append({'argv':a,'cwd':str(D)})
  if not (out/(shader+'.cpp')).exists():
   with(out/'generate.log').open('w')as f:subprocess.run(a,cwd=D,stdout=f,stderr=subprocess.STDOUT,check=True)
  if family=='rms' and tag=='candidate':
   raw=out/(shader+'.cpp');shutil.copy2(raw,out/(shader+'.all-generated.cpp'))
   pattern=r'const uint64_t rms_norm_f32_len = .*?\n};\n'
   selected=re.findall(pattern,raw.read_text(),re.S);assert len(selected)==1
   basecpp=(D/'rms/baseline'/(shader+'.cpp')).read_text();assert len(re.findall(pattern,basecpp,re.S))==1
   raw.write_text(re.sub(pattern,lambda m:selected[0],basecpp,flags=re.S))
   (out/'unselected-generated-spv').mkdir()
   for name in unchanged:
    shutil.move(str(out/'spv'/(name+'.spv')),str(out/'unselected-generated-spv'/(name+'.spv')))
    shutil.copy2(reference/(name+'.spv'),out/'spv'/(name+'.spv'))
  assert sorted(p.stem for p in(out/'spv').glob('*.spv'))==sorted(names)
  for p in(out/'spv').glob('*.spv'):
   if tag=='baseline' or p.stem in unchanged:assert p.read_bytes()==(reference/p.name).read_bytes(),p
   if family=='rms' and p.stem in unchanged:continue
   a=['/usr/bin/spirv-val','--target-env','vulkan1.0',str(p)];commands.append({'argv':a,'cwd':str(D)});subprocess.run(a,check=True)
  if tag=='candidate':modules.update({p.name:{'path':str(p),'sha256':h(p),'bytes':p.stat().st_size}for p in(out/'spv').glob('*.spv')})
  print(family,tag,[(p.name,h(p))for p in(out/'spv').glob('*.spv')],flush=True)
(D/'modules-ready.json').write_text(json.dumps({'status':'PASS current quant6/RMS6 baseline reproduced; unselected RMS5 retained; three new MM and one RMS validated Vulkan1.0; MV3 byte-identical','source_releases':{str(s):h(s/'release.json')for s in[M,R]},'generator':h(gen),'glslc':h(Path('/usr/bin/glslc')),'commands':commands,'modules':modules},indent=2)+'\n')
oldcmd=json.loads((B/'commands.json').read_text());c=next(c for c in oldcmd if c.get('key')=='backend');a=c['argv'][:];a[a.index('-c')+1]=str(M/'candidate/ggml/src/ggml-vulkan/ggml-vulkan.cpp');a[a.index('-o')+1]=str(D/'backend.o');header=Path(br['header']['path']);assert h(header)==br['header']['sha256'];compilecmds=[{'key':'backend','argv':a,'cwd':c['cwd']}]
ccs=json.loads((I/'build/compile_commands.json').read_text())
for family,shader in [('quant','mul_mm_khr_quant.comp'),('rms','rms_norm.comp')]:
 cc=next(x for x in ccs if x['file'].endswith('/'+shader+'.cpp'));a=shlex.split(cc['command']);a[a.index('-c')+1]=str(D/family/'candidate'/(shader+'.cpp'));a[a.index('-o')+1]=str(D/(shader+'.cpp.o'));compilecmds.append({'key':family,'argv':a,'cwd':cc['directory']})
commands+=compilecmds;(D/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
def comp(c):
 with(D/(c['key']+'-compile.log')).open('w')as f:subprocess.run(c['argv'],cwd=c['cwd'],stdout=f,stderr=subprocess.STDOUT,check=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3)as pool:list(pool.map(comp,compilecmds))
shutil.copytree(B/'libs',D/'libs');archive=D/'libs/libggml-vulkan.a';members=subprocess.check_output(['llvm-ar','t',str(archive)],text=True).splitlines()
for n in ['ggml-vulkan.cpp.o','mul_mm_khr_quant.comp.cpp.o','rms_norm.comp.cpp.o']:assert members.count(n)==1
assert hashlib.sha256(subprocess.check_output(['llvm-ar','p',str(archive),'ggml-vulkan.cpp.o'])).hexdigest()==h(B/'backend.o')
shutil.copy2(D/'backend.o',D/'ggml-vulkan.cpp.o');subprocess.run(['llvm-ar','rcs',str(archive),str(D/'ggml-vulkan.cpp.o'),str(D/'mul_mm_khr_quant.comp.cpp.o'),str(D/'rms_norm.comp.cpp.o')],check=True)
subprocess.run(['cp','-a','--reflink=auto',str(B/'cargo-target'),str(D/'cargo-target')],check=True);link=json.loads((B/'link-command.json').read_text());env=dict(link['env']);env['CARGO_TARGET_DIR']=str(D/'cargo-target');env['STRAYLIGHT_ASR_LIB']=str(D/'libs');(D/'libs/STAMP').write_text((D/'libs/STAMP').read_text().strip()+' mm128=d864fa32 rms-runtime=5115cf2c\n');actual={'argv':link['argv'],'cwd':str(D),'env':env};commands.append(actual);(D/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(D/'link-command.json').write_text(json.dumps(actual,indent=2)+'\n')
with(D/'link.log').open('w')as f:subprocess.run(link['argv'],cwd=D,env=dict(os.environ,**env),stdout=f,stderr=subprocess.STDOUT,check=True)
p=D/'cargo-target/aarch64-unknown-linux-gnu/debug/straylight-listen';result={'status':'PASS isolated combined MM128/RMS build; native and numerical gates pending','source_releases':{str(s):h(s/'release.json')for s in[M,R]},'base_result':h(B/'result.json'),'listener':h(p),'libraries':{p.name:h(p)for p in(D/'libs').glob('*.a')},'modules':modules,'header':{'path':str(header),'sha256':h(header)},'commands':h(D/'commands.json'),'scope':'Only backend/generated quant/generated RMS objects over d4d0; all three MV32 modules byte-identical; model/bridge/driver/other modules unchanged; no timing'};(D/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS coherent MM128/RMS',result['listener'],flush=True)
