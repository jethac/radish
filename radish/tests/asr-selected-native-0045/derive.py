#!/usr/bin/env python3
"""Offline selector reconstruction for frozen metadata-only Qwen graphs.
No GGUF payload, Vulkan initialization, compilation, or device use.
"""
from pathlib import Path
import collections, hashlib, json, math
W=Path('/run/media/jethac/Shared/radish')
OUT=Path(__file__).parent
META=W/'asr-metadata-prefix-construction-0045/graphs.jsonl'
BACKEND=W/'asr-profile-cache-ops-se4ykmsi/candidate/ggml/src/ggml-vulkan/ggml-vulkan.cpp'
OLD=W/'asr-graph-profile-ovmz54r9/NATIVE_COMPILE_PLAN.json'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
rows=[json.loads(l) for l in META.read_text().splitlines()]
entries={e['module']:e for e in json.loads(OLD.read_text())['entries']}
# Exact host sizeof(vk_mat_mat_push_constants): seventeen uint32_t fields.
# Module reflection omits the optimized-out final padded_N, but host range is68.
for n in entries:
 if n.startswith('matmul_'):entries[n]['host_push_range_bytes']=68
entries['repeat_i16']={'shader_parameter_count':2,'host_push_range_bytes':120,'specializations':[{'ids':{},'logical_local':[128,1,1]}]}
entries['set_rows_f32_f16_i64']={'shader_parameter_count':3,'host_push_range_bytes':116,'specializations':[{'ids':{'0':1},'logical_local':[128,1,1]}]}
# Pinned ggml type sizes/block sizes; no unsupported type is silently inferred.
T={'f32':(4,1),'f16':(2,1),'i32':(4,1),'i64':(8,1),'q4_K':(144,256),'q6_K':(210,256),'q8_0':(34,32)}
def type_size(t):return T[t['type']][0]
def quant(t):return T[t['type']][1]>1
def contiguous(t):
 sz,bl=T[t['type']];ne=t['ne'];nb=t['nb']
 if ne[0]!=bl and nb[0]!=sz:return False
 expected=sz*ne[0]//bl
 for d in range(1,4):
  if ne[d]!=1 and nb[d]!=expected:return False
  expected*=ne[d]
 return True

def dim01(t):
 ne,nb=t['ne'],t['nb'];sz,bl=T[t['type']]
 return nb[0]==sz and nb[1]==sz*ne[0]//bl and (ne[3]==1 or nb[3]==nb[2]*ne[2])
def permuted(t):return any(t['nb'][i]>t['nb'][i+1] for i in range(3))
def same_shape(a,b):return a['ne']==b['ne']
def cpy(src,dst,to):
 contig=contiguous(src) and (dst is None or contiguous(dst))
 if dst and src['type']==to and same_shape(src,dst):
  if src['nb'][1]==T[to][0]:return 'cpy_transpose_'+str(T[to][0]*8)
  if not contig and src['nb'][2]==T[to][0] and contiguous(dst):return 'cpy_transpose_02_'+str(T[to][0]*8)
 return ('contig_cpy_' if contig else 'cpy_')+src['type']+'_'+to
cases={};node_map=[];matrix=[]
def emit(module,node,reason,variant=0):
 e=entries[module];s=e['specializations'][variant]
 values=tuple(s['ids'][str(i)] for i in range(len(s['ids'])))
 key=(module,values)
 if key not in cases:
  cases[key]={'module':module,'specialization_values':list(values),'logical_local':s['logical_local'],'shader_parameter_count':e['shader_parameter_count'],'push_bytes':e['host_push_range_bytes'],'references':[]}
 ref={'phase':node['phase'],'node':node['node'],'op':node['op'],'reason':reason}
 cases[key]['references'].append(ref)
 return key

def mm(x):
 a,b=x['src'][:2];d=x['tensor'];M,N,K=a['ne'][1],b['ne'][1],a['ne'][0]
 assert d['ne'][0]==M and d['ne'][1]==N
 r2=b['ne'][2]//a['ne'][2];r3=b['ne'][3]//a['ne'][3]
 assert b['ne'][2]%a['ne'][2]==0 and b['ne'][3]%a['ne'][3]==0
 info={'phase':x['phase'],'node':x['node'],'M':M,'N':N,'K':K,'src0':a,'src1':b,'destination':d,'r2':r2,'r3':r3,'src0_contiguous':contiguous(a),'src1_contiguous':contiguous(b),'src0_permuted':permuted(a),'src1_permuted':permuted(b),'selected':[]}
 # Exact top-level p021/NC conditions, before ordinary matvec. All actual
 # post-REPEAT cache tensors in this inventory are contiguous, so neither fires.
 p021=a['type']=='f16' and permuted(a) and permuted(b) and N==1 and all(t['nb'][0]<=t['nb'][2]<=t['nb'][1]<=t['nb'][3] and t['ne'][3]==1 for t in (a,b))
 nc=a['type']=='f16' and not contiguous(a) and b['nb'][0]<=b['nb'][1] and N==1 and not permuted(a) and not permuted(b)
 assert not p021 and not nc,('new specialized route needs exact profile specs',x)
 keys=[]
 if N==1 or (N<=8 and b['ne'][2]*b['ne'][3]==1):
  assert b['type']=='f32'
  if not dim01(a):keys.append(emit(cpy(a,None,a['type']),x,'ordinary matvec contiguous A staging'))
  if not dim01(b):keys.append(emit(cpy(b,None,b['type']),x,'ordinary matvec contiguous B staging'))
  assert 1<=N<=8
  module='mul_mat_vec_'+a['type'].lower()+'_f32_f32'
  keys.append(emit(module,x,'ordinary matvec, actual N='+str(N),N-1))
  info['route']='ordinary matvec'
 else:
  at=a['type'];bt=b['type']
  # No coop/intdot on current real candidate; profile rejects direct quant MM.
  if not dim01(a):
   keys.append(emit(cpy(a,None,'f16'),x,'matrix A noncontiguous F16 staging'));at='f16'
  elif quant(a):
   keys.append(emit('dequant_'+at.lower(),x,'matrix A quantized→F16 staging'));at='f16'
  if not dim01(b):
   keys.append(emit(cpy(b,None,'f16'),x,'matrix B noncontiguous F16 staging'));bt='f16'
  assert at in ('f16','f32') and bt in ('f16','f32')
  module='matmul_'+at+('_'+bt if bt=='f32' or at=='f32' else '')+'_fp32'
  # Existing float tc_mm is filtered with conservative MMQ BK32 estimates:
  # (BM+BN)*(32+1)*4 =8448/16896/33792 vs maxShared16384.
  # Hence only Small remains. This is existing policy, not hardware evidence
  # that an actual floating Medium BK16 configuration is impossible.
  aligned=K%32==0 and M>8 and N>8
  keys.append(emit(module,x,'Small-only existing filter; K32 alignment='+str(aligned),int(aligned)))
  info['route']='matrix Small';info['aligned']=aligned
  # Current Qualcomm backend has no NV/AMD/Intel core-count query, defaults0;
  # ggml_vk_guess_split_k returns1. No reduction shader selected here.
 info['selected']=[{'module':k[0],'specs':list(k[1])} for k in keys];matrix.append(info)
 return keys

metadata={'RESHAPE','VIEW','PERMUTE','TRANSPOSE'}
for x in rows:
 if 'op' not in x:continue
 op=x['op'];s=x['src'][0];d=x['tensor'];keys=[]
 if op in metadata:continue
 if op=='MUL_MAT':keys=mm(x)
 elif op in ('CONT','CPY'):keys=[emit(cpy(s,d,d['type']),x,'exact ggml_vk_get_cpy_pipeline predicates')]
 elif op=='GET_ROWS':
  assert x['src'][1]['type']=='i32'
  keys=[emit('get_rows_'+s['type'].lower()+'_f32',x,'actual embedding tensor/index types')]
 elif op=='NORM':keys=[emit('norm_f32',x,'unfused NORM')]
 elif op=='RMS_NORM':keys=[emit('rms_norm_f32',x,'profile disables optional fusion; specs0,0')]
 elif op in ('ADD','MUL'):keys=[emit(op.lower()+'_f32_f32_f32',x,'unfused binary F32')]
 elif op=='UNARY':
  assert x['op_params_i32'][0]==16;keys=[emit('gelu_erf_f32',x,'actual unary enum16=GELU_ERF')]
 elif op=='GLU':
  assert x['op_params_i32'][0]==2;keys=[emit('swiglu_f32',x,'actual GLU enum2=SWIGLU')]
 elif op=='ROPE':
  assert x['op_params_i32'][2]==2;keys=[emit('rope_neox_f32',x,'actual mode2=NEOX, not model metadata MROPE')]
 elif op=='SOFT_MAX':
  assert s['ne'][0]<=1024
  mask=x['src'][1];keys=[emit('soft_max_f32_f16' if mask else 'soft_max_f32',x,'actual row width<=1024→BLOCK_SIZE32')]
 elif op=='IM2COL':keys=[emit('im2col_f32_f16' if d['type']=='f16' else 'im2col_f32',x,'actual destination storage type')]
 elif op=='CONCAT':keys=[emit('concat_i32',x,'F32 raw32 concatenation')]
 elif op=='ARGMAX':keys=[emit('argmax_f32',x,'actual F32 logits')]
 elif op=='REPEAT':
  assert s['type']==d['type']=='f16';keys=[emit('repeat_i16',x,'materialize duplicated GQA cache head dimension')]
 elif op=='SET_ROWS':
  assert s['type']=='f32' and d['type']=='f16' and x['src'][1]['type']=='i64'
  keys=[emit('set_rows_f32_f16_i64',x,'KV index producer domain0..335, highword0; no generic Int64 arithmetic')]
 else:raise AssertionError(('unmapped actual op',op))
 node_map.append({'phase':x['phase'],'node':x['node'],'op':op,'selected':[{'module':k[0],'specs':list(k[1])} for k in keys]})
# Preserve first-use case order; modules already independently built/validated.
manifest=[];lines=[]
for i,(key,c) in enumerate(cases.items()):
 n=c['module'];base=W/('asr-cache-ops-build-0045' if n in ('repeat_i16','set_rows_f32_f16_i64') else 'asr-graph-bounded-build-0045')/'modules'/n
 p=base/(n+'.spv');assert p.is_file()
 c['case_id']='case%02d'%i;c['module_path']=str(p);c['module_sha256']=sha(p)
 # Required module SPIR-V1.0; independent host capability validator already ran.
 data=p.read_bytes();assert int.from_bytes(data[4:8],'little')==0x10000
 vals=[c['shader_parameter_count'],c['push_bytes'],*c['logical_local'],len(c['specialization_values']),*c['specialization_values']]
 lines.append(c['case_id']+' '+n+' '+' '.join(map(str,vals)))
 manifest.append(c)
assert len(manifest)<=64
(OUT/'selected-plan.txt').write_text('# Frozen metadata-selected compile-only cases; no tensors or dispatch.\n'+'\n'.join(lines)+'\n')
(OUT/'cases.json').write_text(json.dumps(manifest,indent=2)+'\n')
(OUT/'node-selection.json').write_text(json.dumps(node_map,indent=2)+'\n')
(OUT/'matrix-selection.json').write_text(json.dumps(matrix,indent=2)+'\n')
report={'status':'Offline exact metadata/source selector reconstruction, pending independent review and native compile','metadata':{'path':str(META),'sha256':sha(META),'records':len(rows),'phase_summaries':[r for r in rows if 'op' not in r]},'backend':{'path':str(BACKEND),'sha256':sha(BACKEND)},'cases':len(manifest),'modules':len(set(c['module'] for c in manifest)),'mapped_compute_nodes':len(node_map),'matrix_nodes':len(matrix),'effective_ratios':dict(collections.Counter(str((m['r2'],m['r3'])) for m in matrix)),'scope':['Actual cold_prefix9_kv272 and retained_prefix9_kv336 are separate metadata phases. Full empty-audio prompt ids15 influences initial KV capacity272, not an invented15-row graph. Cold76/cached67/step336 and representative audio61/subsample5 are preserved.','Profile disables optional backend fusions; nonflash GPU graph retained. Source-derived selector evidence is not an actual backend/device trace.','Current floating matrix tile filtering uses MMQ BK32 estimate; only Small. This conservative policy is a future performance issue, not proof that real Medium hardware resource use is illegal.','Generic Q6 multi-column M-split implementation remains required source capability but this metadata graph ties final logits to N1, selecting direct Q6 matvec. No311MB dequant temporary selected here.','SET_ROWS i64 name uses pinned uvec2 lowword path only for producer indices0..335/highword0. Both new modules carry Shader+Storage16 and no Int64.','Native compile helper requests real public core robustBufferAccess plus KHR8/16/Int8/timeline; no feature suppression. Compile only, no buffers/commands/submits.','Host range for matrix pushes is68 bytes (17U32), while SPIR-V optimized block reports64. This corrects the old registration-domain planning note, without changing backend source.','Full ASR numerical and full peak-memory admission remain separate. Host x86 backend/listener links do not establish AArch64 production linkage.'],'artifacts':{p.name:sha(p) for p in (OUT/'selected-plan.txt',OUT/'cases.json',OUT/'node-selection.json',OUT/'matrix-selection.json',Path(__file__))}}
(OUT/'release.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ('cases','modules','mapped_compute_nodes','matrix_nodes','effective_ratios')},indent=2))
print('plan',sha(OUT/'selected-plan.txt'),'release',sha(OUT/'release.json'))
for c in manifest:print(c['case_id'],c['module'],c['specialization_values'],len(c['references']))
