#!/usr/bin/env python3
"""Usage: benchmark_stats.py BASELINE CURRENT [RUNS=10]. Alternated host trials.
Build both executables from tests/stats_benchmark_host.cpp with identical -O3
flags; baseline uses the pre-statistics engine and -DSTATS_BASELINE.
"""
import json, os, statistics, subprocess, sys
baseline,current=sys.argv[1:3]
runs=int(sys.argv[3]) if len(sys.argv)>3 else 10
assert runs>=10
report={'conditions':'Synthetic Android native renderer on host, 48 kHz, 256-frame blocks, 32 slots, twenty seconds/case; macOS user-interactive QoS for consistent core selection; one warmup per mode, >=10 alternating trials, 5 Hz native snapshots. Includes native command publication; excludes JavaScript/JNI, scheduler jitter and hardware. p95 is elapsed block time. No device thermal, battery or glitch claim.', 'runs':runs,'cases':[]}
for spatial in [False,True]:
 if os.getenv("SYNTH_BENCH_SPATIAL") is not None and spatial != (os.environ["SYNTH_BENCH_SPATIAL"]=="1"): continue
 for scenario,name in enumerate(['silence','oneVoice','32Voices','steals','chords','patterns','drain']):
  if os.getenv('SYNTH_BENCH_CASE') is not None and scenario != int(os.environ['SYNTH_BENCH_CASE']): continue
  trials={m:[] for m in ['previous','basic','full']}
  for run in range(runs+1):
   modes=list(trials)
   if run%2: modes.reverse()
   for mode in modes:
    env=os.environ.copy(); env.pop('SYNTH_STATS_FULL',None)
    if mode=='full': env['SYNTH_STATS_FULL']='1'
    result=json.loads(subprocess.check_output([baseline if mode=='previous' else current,str(scenario),str(int(spatial))],env=env))
    if run: trials[mode].append(result)
  hashes={r['hash'] for rows in trials.values() for r in rows}
  if len(hashes)!=1: raise RuntimeError(f'Output changed: {name}, spatial={spatial}')
  med={m:{k:statistics.median(r[k] for r in rows) for k in ['renderMs','p95Ms']} for m,rows in trials.items()}
  changes={m:{k:100*(med[m][k]/med['previous'][k]-1) for k in med[m]} for m in ['basic','full']}
  report['cases'].append({'name':name,'spatial':spatial,'median':med,'changePercent':changes,'identicalOutput':True,'trials':trials})
print(json.dumps(report,indent=2))
