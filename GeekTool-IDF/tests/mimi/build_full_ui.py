"""完整资源、真实LVGL40行和后台生命周期证明。 / Prove full assets through real LVGL40-row drawing and worker lifetimes."""
from pathlib import Path
import hashlib,json,os,shlex,subprocess,sys,tempfile
sys.dont_write_bytecode=True
here=Path(__file__).resolve().parent;project=here.parents[1];port=here/'pipeline_port';host=project/'build/mimi-full-host/mimi';assets=Path(os.environ.get('MIMI_TEST_ASSETS',str(project/'build/flash-records/mimi-full-20261103/assets')));record=Path(os.environ.get('MIMI_TEST_RECORDS',str(assets.parent/'pipeline')));record.mkdir(parents=True,exist_ok=True)
manifest=json.loads((assets/'manifest.json').read_text());pack=assets/'MIMI.PAK';assert hashlib.sha256(pack.read_bytes()).hexdigest()==manifest['sha256'];assert manifest['frame_count']==186
sources=[here/'full_ui_tests.c',Path(__file__),project/'tools/mimi_assets/native_ui.h']+[project/'main'/n for n in ['app_mimi.c','mimi_assets.c','mimi_assets.h','mimi_pipeline.c','mimi_pipeline.h','mimi_prepare.c','mimi_prepare.h','mimi_render.c','mimi_render.h']]+[port/'host_kernel.c',port/'test_hooks.h']
before={str(p.relative_to(project)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources};results={}
for mode,extra in [('normal',[]),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 with tempfile.TemporaryDirectory(prefix='mimi-full-ui-') as temp:
  temp=Path(temp)
  expected=[]
  for name,field in [('expected_native','reference_crc32'),('expected_swapped','swapped_crc32'),('expected_clip','clip'),('expected_ms','ms')]:expected.append('static const uint32_t '+name+'[186]={'+','.join(str(f[field])+'u' for f in manifest['frames'])+'};')
  (temp/'full_expected.inc').write_text('\n'.join(expected)+'\n')
  base=['/usr/bin/cc','-O2','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Wno-unused-function','-DLV_CONF_PATH="'+str(project/'tests/host/lv_conf.h')+'"','-DLV_KCONFIG_IGNORE','-DLV_LVGL_H_INCLUDE_SIMPLE','-I'+str(temp),'-I'+str(port/'stubs'),'-I'+str(port),'-I'+str(project/'tests/host/stubs'),'-I'+str(project/'main'),'-I'+str(project/'managed_components/lvgl__lvgl'),'-I'+str(project/'tests/host')]+extra
  units=[('ui',here/'full_ui_tests.c',['-DMIMI_FULL_HOST=1']),('render',project/'main/mimi_render.c',[]),('assets',project/'main/mimi_assets.c',['-Dmalloc=full_asset_malloc','-Dfree=full_asset_free']),('backend',project/'main/mimi_pipeline.c',['-DESP_PLATFORM']),('prepare',project/'main/mimi_prepare.c',[]),('kernel',port/'host_kernel.c',['-DESP_PLATFORM'])]
  objects=[]
  with (record/('full-ui-'+mode+'-build.log')).open('w') as log:
   for name,source,defines in units:
    obj=temp/(name+'.o');objects.append(obj);subprocess.run(base+defines+['-c',str(source),'-o',str(obj)],check=True,stdout=log,stderr=subprocess.STDOUT)
   link=shlex.split((host/'CMakeFiles/mimi_ui_tests.dir/link.txt').read_text());link=[str(objects[0]) if x=='CMakeFiles/mimi_ui_tests.dir/ui_tests.c.o' else x for x in link if not any(x.endswith('/'+n+'.c.o') or x.endswith('/'+n+'.o') for n in ['mimi_assets','mimi_pipeline','mimi_prepare','mimi_render','mimi_paint'])]
   archive=next(i for i,x in enumerate(link) if x.endswith('liblvgl.a'));link[archive:archive]=[str(p) for p in objects[1:]];exe=temp/'full_ui_tests';link[link.index('-o')+1]=str(exe);link+=['-pthread','-lz']+extra
   subprocess.run(link,cwd=host,check=True,stdout=log,stderr=subprocess.STDOUT)
  run=subprocess.run([str(exe),str(pack)],text=True,capture_output=True);(record/('full-ui-'+mode+'-run.log')).write_text(run.stdout+run.stderr)
  if run.returncode:raise RuntimeError(mode+' failed '+str(run.returncode)+'; see '+str(record/('full-ui-'+mode+'-run.log')))
  print(mode+': '+run.stdout,end='');assert 'full_ui_native_comparisons=372 dynamic_eye_comparisons=450 fallback_paths=28 early_poll_checks=200 ordinary_gate_checks=2 exact=true' in run.stdout
  results[mode]={'passed':True,'native_comparisons':372,'dynamic_eye_comparisons':450,'fallback_paths':28,'early_poll_checks':200,'ordinary_gate_checks':2,'baseline_lvgl_not_sanitized':True,'modules_sanitized':mode=='asan_ubsan'}
after={str(p.relative_to(project)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources};assert before==after,'Source changed during integration proof'
(record/'full-ui-proof.json').write_text(json.dumps({'host_only':True,'actual_esp_stack_not_measured':True,'pack_sha256':manifest['sha256'],'results':results,'sources':after},indent=2)+'\n')
