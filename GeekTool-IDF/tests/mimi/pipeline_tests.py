"""生产双槽队列/生命周期的pthread验证。 / Exercise production two-slot queues and lifetimes through a pthread port."""
from pathlib import Path
import hashlib,json,subprocess,tempfile,sys
sys.dont_write_bytecode=True
here=Path(__file__).resolve().parent;project=here.parents[1];port=here/'pipeline_port';record=project/'build/flash-records/mimi-full-20261103/pipeline';record.mkdir(parents=True,exist_ok=True)
base=['/usr/bin/cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DESP_PLATFORM','-Wall','-Wextra','-Werror','-I'+str(port/'stubs'),'-I'+str(port),'-I'+str(project/'main'),str(here/'pipeline_tests.c'),str(port/'host_kernel.c'),str(project/'main/mimi_pipeline.c'),str(project/'main/mimi_prepare.c'),str(project/'main/mimi_motion.c'),str(project/'main/mimi_pet.c'),'-pthread','-lm']
results={}
with tempfile.TemporaryDirectory(prefix='mimi-pipeline-') as temp:
 for mode,flags in [('normal',['-O2']),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('tsan',['-O1','-g','-fsanitize=thread','-fno-omit-frame-pointer'])]:
  exe=Path(temp)/mode
  with (record/('pipeline-'+mode+'-build.log')).open('w') as log:subprocess.run(base+flags+['-o',str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True)
  run=subprocess.run([str(exe)],text=True,capture_output=True);(record/('pipeline-'+mode+'-run.log')).write_text(run.stdout+run.stderr)
  if run.returncode:raise RuntimeError(mode+' failed; see '+str(record/('pipeline-'+mode+'-run.log')))
  print(mode+': '+run.stdout,end='');results[mode]={'passed':True,'summary':run.stdout.strip()}
files=[here/'pipeline_tests.c',Path(__file__),project/'main/mimi_pipeline.c',project/'main/mimi_pipeline.h',project/'main/mimi_prepare.c',project/'main/mimi_prepare.h']+sorted(p for p in port.rglob('*') if p.is_file())
(record/'pipeline-proof.json').write_text(json.dumps({'host_only':True,'results':results,'real_asset_reader_mocked':True,'actual_esp_stack_not_measured':True,'sources':{str(p.relative_to(project)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')
