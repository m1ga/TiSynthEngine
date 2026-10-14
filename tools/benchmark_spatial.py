#!/usr/bin/env python3
"""Compare stereo/binaural replay CPU time using spatial_benchmark_host.
Usage: python3 tools/benchmark_spatial.py RENDERER TONE_TIMELINE.txt [...]
Input format is the 25-column tone timeline used by Asteroids' replay tools.
Numbers are host replay wall time, not device CPU, thermal or underrun measurements.
"""
import json, os, subprocess, sys
from pathlib import Path
renderer = str(Path(sys.argv[1]).resolve())
report = {'conditions': 'Current Android engine, optimized host build, 48 kHz, 32 voices. Synthetic replay, no hardware stream or JS bridge. All events spatial in binaural mode. Median of five runs after one warmup.', 'cases': []}
for name in sys.argv[2:]:
    data = Path(name).read_bytes()
    modes = {}
    for mode in ['stereo', 'binaural']:
        env = os.environ.copy()
        env['SYNTH_BINAURAL'] = '1' if mode == 'binaural' else '0'
        trials = []
        for run in range(6):
            result = json.loads(subprocess.check_output([renderer], input=data, env=env))
            if result['nonFiniteFrames']:
                raise RuntimeError('Renderer produced non-finite output')
            if run:
                trials.append(result)
        modes[mode] = sorted(trials, key=lambda r: r['audioCpuMs'])[2]
    report['cases'].append({'name': Path(name).stem, 'modes': modes, 'cpuTimeRatio': modes['binaural']['audioCpuMs'] / modes['stereo']['audioCpuMs']})
print(json.dumps(report, indent=2))
