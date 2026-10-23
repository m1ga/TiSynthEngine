#!/usr/bin/env python3
import subprocess,json,time,sys,pathlib,hashlib,zipfile,re,datetime
import argparse
parser=argparse.ArgumentParser(description='Run a diagnostic phase in the separate Titanium physical-diagnostic app; preserve all other app data.')
parser.add_argument('phase',choices=['feasibility','memory','controlled'])
parser.add_argument('duration_ms',type=int)
parser.add_argument('--serial',required=True)
parser.add_argument('--package',default='ti.synthengine.physicaldiagnostic')
parser.add_argument('--output',type=pathlib.Path,required=True)
parser.add_argument('--config',default='{}',help='JSON phase options, for example warmupMs/spatial/lfos/effects')
args=parser.parse_args()
if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+',args.package):parser.error('Invalid Android package name')
if args.duration_ms<1:parser.error('duration_ms must be positive')
serial=args.serial;pkg=args.package;phase=args.phase;duration=args.duration_ms;out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
def adb(*args,**kw):return subprocess.run(['adb','-s',serial,*args],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,**kw)
adb('shell','am','force-stop',pkg)
adb('shell','run-as',pkg,'mkdir','-p','app_appdata')
cfg={'phase':phase,'durationMs':duration};cfg.update(json.loads(args.config));config=json.dumps(cfg)
r=adb('shell',f"run-as {pkg} sh -c 'cat > app_appdata/physical-config.json'",input=config);assert r.returncode==0,r.stderr
adb('shell','run-as',pkg,'rm','-f','app_appdata/physical-result.json','app_appdata/physical-progress.json')
launch=adb('shell','monkey','-p',pkg,'-c','android.intent.category.LAUNCHER','1');(out/'launch.txt').write_text(launch.stdout+launch.stderr)
start=time.monotonic();last_phase=None;memory=[];progress=[];timeout=max(duration/1000+65,100)
while time.monotonic()-start<timeout:
 r=adb('shell','run-as',pkg,'cat','app_appdata/physical-progress.json')
 try:p=json.loads(r.stdout)
 except Exception:p={}
 pid=adb('shell','pidof',pkg).stdout.strip().split(' ')[0]
 status=adb('shell','run-as',pkg,'cat','/proc/'+pid+'/status').stdout if pid else ''
 values={m.group(1):int(m.group(2)) for m in re.finditer(r'^(VmRSS|VmHWM|VmSize|RssAnon|RssFile):\s+(\d+) kB',status,re.M)}
 freq=adb('shell',"for c in 0 6; do echo CPU$c; cat /sys/devices/system/cpu/cpu$c/cpufreq/scaling_cur_freq; done").stdout.strip().splitlines()
 row={'hostElapsedSeconds':time.monotonic()-start,'phase':p.get('phase'),'appElapsedMs':p.get('elapsedMs'),'pid':pid,'memoryKiB':values,'cpuFrequencyRaw':freq};memory.append(row)
 if p:progress.append(p)
 if p.get('phase')!=last_phase:
  last_phase=p.get('phase');print(phase,'stage',last_phase,'RSS',values.get('VmRSS'),flush=True)
  if last_phase in ['warmup','finished']:
   (out/('thermal-'+last_phase+'.txt')).write_text(adb('shell','dumpsys','thermalservice').stdout)
   (out/('cpu-time-in-state-'+last_phase+'.txt')).write_text(adb('shell',"for c in 0 6; do echo CPU$c; cat /sys/devices/system/cpu/cpu$c/cpufreq/stats/time_in_state; done").stdout)
  if last_phase=='warmup':
   (out/'performance-hint.txt').write_text(adb('shell','dumpsys','performance_hint').stdout)
   (out/'audio-flinger-active.txt').write_text(adb('shell','dumpsys','media.audio_flinger').stdout)
  if phase=='memory' or last_phase=='baseline':
   dump=adb('shell','dumpsys','meminfo',pkg,'-d');(out/('meminfo-'+str(last_phase)+'.txt')).write_text(dump.stdout)
 if p.get('phase')=='measurement' and len(memory)%15==0:print(phase,'elapsed',p.get('elapsedMs'),'peakLoad',p.get('stats',{}).get('peakRenderLoad'),'active',p.get('stats',{}).get('activeVoices'),flush=True)
 result=adb('shell','run-as',pkg,'cat','app_appdata/physical-result.json')
 try:final=json.loads(result.stdout)
 except Exception:final=None
 if final and final.get('complete'):
  (out/'result.json').write_text(json.dumps(final,indent=2)+'\n');break
 time.sleep(1)
else:print('TIMEOUT',phase,flush=True)
(out/'memory.json').write_text(json.dumps(memory,indent=2)+'\n');(out/'progress.json').write_text(json.dumps(progress,indent=2)+'\n')
logs=adb('logcat','-d','-v','threadtime','--pid',pid).stdout if pid else '';(out/'logcat.txt').write_text(logs)
print('completed',phase,'duration',time.monotonic()-start,'result',bool(final),flush=True)
