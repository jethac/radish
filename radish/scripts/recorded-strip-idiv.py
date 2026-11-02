from pathlib import Path
import hashlib,json,struct,subprocess
W=Path('/run/media/jethac/Shared/radish');D=W/'compute11-idiv-isolated-strip-0045';D.mkdir()
inputs={'idiv':W/'compute11-idiv-isolated-build-0045/libvulkan_radish.idiv.so'}
def pin(p):return {'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
def parse(p):
 b=p.read_bytes();assert b[:6]==b'\x7fELF\x02\x01';h=struct.unpack_from('<16sHHIQQQIHHHHHH',b);po,so,pe,pn,se,sn,si=h[5],h[6],h[9],h[10],h[11],h[12],h[13];assert pe==56 and se==64 and sn<65535 and si<sn
 sh=[struct.unpack_from('<IIQQQQIIQQ',b,so+i*se) for i in range(sn)];ss=sh[si];st=b[ss[4]:ss[4]+ss[5]];sections={}
 for v in sh:
  name=st[v[0]:].split(b'\0',1)[0].decode();sections[name]={'header':v,'bytes':b'' if v[1]==8 else b[v[4]:v[4]+v[5]]}
 ph=[struct.unpack_from('<IIQQQQQQ',b,po+i*pe) for i in range(pn)]
 return b,h,sections,ph,b[po:po+pn*pe]
results=[]
for label,src in inputs.items():
 before=pin(src);out=D/label;out.mkdir();dst=out/src.name;cmd=['llvm-objcopy','--strip-debug','--keep-section=.debug_gdb_scripts',str(src),str(dst)];subprocess.run(cmd,check=True)
 a,ha,sa,pa,pba=parse(src);b,hb,sb,pb,pbb=parse(dst);assert pba==pbb and pa==pb
 allowed_header_bytes=set(range(40,48))|set(range(60,64));diff=[i for i in range(64) if a[i]!=b[i]];assert set(diff)<=allowed_header_bytes,diff
 alloc_a={n:x for n,x in sa.items() if x['header'][2]&2};alloc_b={n:x for n,x in sb.items() if x['header'][2]&2};assert alloc_a.keys()==alloc_b.keys()
 proof=[]
 for name,x in alloc_a.items():
  y=alloc_b[name];xh,yh=x['header'],y['header'];assert xh[1:6]==yh[1:6] and xh[8:]==yh[8:] and x['bytes']==y['bytes'],name
  for idx in [6]:
   if xh[idx]:
    anext=next(k for k,v in sa.items() if v['header']==struct.unpack_from('<IIQQQQIIQQ',a,ha[6]+xh[idx]*64));bnext=next(k for k,v in sb.items() if v['header']==struct.unpack_from('<IIQQQQIIQQ',b,hb[6]+yh[idx]*64));assert anext==bnext
  assert xh[7]==yh[7],name
  proof.append({'section':name,'address':xh[3],'offset':xh[4],'size':xh[5],'sha256':hashlib.sha256(x['bytes']).hexdigest()})
 for ph in pa:
  if ph[0]!=1:continue
  start,size=ph[2],ph[5];aa=bytearray(a[start:start+size]);bb=bytearray(b[start:start+size]);assert len(aa)==len(bb)
  for pos in allowed_header_bytes:
   if start<=pos<start+size:aa[pos-start]=bb[pos-start]
  assert aa==bb,'PT_LOAD content outside section-table header fields changed'
 explicit={n:hashlib.sha256(sa[n]['bytes']).hexdigest() for n in sa if n in ['.dynsym','.dynstr','.dynamic','.rela.dyn','.rela.plt','.note.gnu.build-id','.gnu.version','.gnu.version_r','.gnu.hash']}
 for n in explicit:assert sa[n]['bytes']==sb[n]['bytes']
 assert pin(src)==before
 removed=[n for n in sa if n not in sb];assert all(n.startswith('.debug_') or n.startswith('.zdebug_') for n in removed),removed
 item={'label':label,'original_path':str(src),'original':before,'derivative_path':str(dst),'derivative':pin(dst),'saved_bytes':before['bytes']-dst.stat().st_size,'elf_machine':ha[2],'program_headers_byte_exact':True,'allocated_sections_byte_exact_at_same_addresses_and_file_offsets':proof,'load_segments_exact_except_expected_elf_section_table_header_fields':True,'elf_header_changed_offsets':diff,'explicit_dynamic_relocation_build_id_hashes':explicit,'removed_nonallocated_debug_sections':removed,'command':cmd,'scope':'Debug-only storage derivative; allocated .debug_gdb_scripts explicitly retained. Original immutable. Host listener remains x86_64 host-only, not tablet deployable.' if ha[2]==62 else 'Debug-only AArch64 staging derivative; source/runtime code and every allocated section unchanged. Original immutable. Original device/native/numerical/resource guards remain.'}
 (out/'proof.json').write_text(json.dumps(item,indent=2)+'\n');results.append(item);print(label,before['bytes'],'->',dst.stat().st_size,pin(dst)['sha256'],flush=True)
(D/'result.json').write_text(json.dumps({'status':'debug-only derivative passed exact ELF identity proof','results':results,'limitation':'Only ELF section table header fields/section table/nonallocated debug data differ. This does not qualify new source/runtime behavior or AArch64 ASR deployment.'},indent=2)+'\n')
