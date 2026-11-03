"""完整姿态眼睛准备的独立完整遮罩对照。 / Compare all-pose eye preparation with the frozen full-mask oracle."""
from pathlib import Path
import hashlib,json,subprocess,tempfile,sys
sys.dont_write_bytecode=True
here=Path(__file__).resolve().parent;project=here.parents[1];record=project/'build/flash-records/mimi-full-20261103/pipeline';record.mkdir(parents=True,exist_ok=True)
base=['/usr/bin/cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(project/'main'),str(here/'prepare_tests.c'),str(here/'prepare_reference.c'),str(project/'main/mimi_prepare.c'),str(project/'main/mimi_motion.c'),str(project/'main/mimi_pet.c'),'-pthread','-lm']
results={}
with tempfile.TemporaryDirectory(prefix='mimi-prepare-') as temp:
 for mode,flags in [('normal',['-O2']),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
  exe=Path(temp)/mode
  with (record/('prepare-'+mode+'-build.log')).open('w') as log:subprocess.run(base+flags+['-o',str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True)
  run=subprocess.run([str(exe)],text=True,capture_output=True);(record/('prepare-'+mode+'-run.log')).write_text(run.stdout+run.stderr)
  if run.returncode:raise RuntimeError(mode+' failed; see '+str(record/('prepare-'+mode+'-run.log')))
  print(mode+': '+run.stdout,end='');results[mode]={'passed':True,'summary':run.stdout.strip()}
files=[here/'prepare_tests.c',here/'prepare_reference.c',Path(__file__),project/'main/mimi_prepare.c',project/'main/mimi_prepare.h',project/'main/mimi_motion.c',project/'main/mimi_pet.c']
(record/'prepare-proof.json').write_text(json.dumps({'host_only':True,'results':results,'native_lvgl_checked_by_integration':False,'frozen_reference':'original full-mask reference SHA6ccdb6f49d5a9ac10feea58b7dd73692f416c2cf3d51750dfe351071428299ca','sources':{str(p.relative_to(project)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')
