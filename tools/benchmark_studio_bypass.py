#!/usr/bin/env python3
"""Compare a preserved pre-Studio native engine against the current bypass.

Build first with --build-only while other builds run. After all builds stop,
repeat with --run-only using the same work directory. No module/app is changed.
Both executables use stats_benchmark_host.cpp, identical -O3 flags, disabled full
statistics and identical 5 Hz native snapshot polls. Never uses STATS_BASELINE.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import statistics
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args):
    root = args.module_root.resolve()
    oboe = next(pathlib.Path.home().joinpath('.gradle/caches').glob('**/oboe-*/prefab/modules/oboe/include'), None)
    if oboe is None:
        raise SystemExit('Locally cached Oboe headers are required')
    java = pathlib.Path(subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
    flags = ['clang++', '-std=c++17', '-O3', '-Wall', '-I' + str(oboe),
             '-I' + str(java / 'include'), '-I' + str(java / 'include/darwin'),
             '-DHAS_EXPRESSIVE', '-Wl,-undefined,dynamic_lookup']
    args.work_dir.mkdir(parents=True, exist_ok=True)
    builds = {}
    for mode, source in [('baseline', args.baseline_source.resolve()),
                         ('current', root / 'android/jni/SynthOboeEngine.cpp')]:
        for kind, harness in [('render', 'engine_render_host.cpp'), ('benchmark', 'stats_benchmark_host.cpp')]:
            binary = args.work_dir / f'{mode}-{kind}'
            command = flags + ['-DENGINE_CPP=' + json.dumps(str(source)),
                               str(root / 'tests' / harness), '-o', str(binary)]
            subprocess.run(command, cwd=root, check=True)
            builds[f'{mode}-{kind}'] = {'command': command, 'binarySha256': digest(binary)}
    expected = re.findall(r'scenario (.*?)hash=([a-f0-9]+)', args.baseline_render_log.read_text())
    if len(expected) != 9:
        raise SystemExit('Expected exactly nine pre-plan reference render hashes')
    for mode in ['baseline', 'current']:
        run = subprocess.run([str(args.work_dir / f'{mode}-render')], text=True, stdout=subprocess.PIPE)
        actual = re.findall(r'scenario (.*?)hash=([a-f0-9]+)', run.stdout)
        (args.work_dir / f'{mode}-render.log').write_text(run.stdout)
        if run.returncode or actual != expected:
            raise SystemExit(f'{mode}: expressive checks or exact legacy hashes failed')
    metadata = {'baselineSource': str(args.baseline_source.resolve()),
                'baselineSourceSha256': digest(args.baseline_source),
                'currentSourceSha256': digest(root / 'android/jni/SynthOboeEngine.cpp'),
                'legacyHashCount': 9, 'legacyHashesMatch': True, 'expressiveChecksPass': True,
                'builds': builds,
                'baselineFidelity': 'Preserved pre-Studio native engine: renderTail identical; held selection, edge guards, live controls and envelopes unchanged; limiter arithmetic matches apart variable names/format. Both builds resolve shared headers from the current repository, not preserved historical headers.'}
    (args.work_dir / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')


def measure(args):
    metadata = json.loads((args.work_dir / 'metadata.json').read_text())
    for name, info in metadata['builds'].items():
        if digest(args.work_dir / name) != info['binarySha256']:
            raise SystemExit(f'Binary changed since validation: {name}')
    env = os.environ.copy()
    env.pop('SYNTH_STATS_FULL', None)
    report = dict(metadata, runs=args.runs, fullStatisticsEnabled=False,
                  conditions='Synthetic native Android callback on macOS host; user-interactive QoS; 48kHz, 256-frame blocks, 32 slots, twenty seconds per trial; 5Hz snapshots in both builds; one warmup then >=10 alternating sequential trials. Elapsed render totals and block p95. Host wall timing includes scheduler stalls. No hardware realtime, thermal, battery or glitch acceptance claim.',
                  limitsPercent={'renderMs': 2, 'p95Ms': 5}, cases=[])
    for spatial in [False, True]:
        if args.spatial is not None and spatial != bool(args.spatial):
            continue
        for scenario, name in [(1, 'oneVoice'), (2, '32Voices')]:
            if args.case is not None and scenario != args.case:
                continue
            trials = {'baseline': [], 'current': []}
            for run in range(args.runs + 1):
                order = ['baseline', 'current'] if run % 2 == 0 else ['current', 'baseline']
                for mode in order:
                    binary = args.work_dir / f'{mode}-benchmark'
                    result = json.loads(subprocess.check_output([str(binary), str(scenario), str(int(spatial))], env=env, text=True))
                    if run:
                        trials[mode].append(result)
            hashes = {row['hash'] for rows in trials.values() for row in rows}
            if len(hashes) != 1:
                raise SystemExit(f'Benchmark output changed: {name}, spatial={spatial}')
            medians = {mode: {key: statistics.median(row[key] for row in rows)
                              for key in ['renderMs', 'p95Ms']} for mode, rows in trials.items()}
            changes = {key: 100 * (medians['current'][key] / medians['baseline'][key] - 1)
                       for key in ['renderMs', 'p95Ms']}
            within = changes['renderMs'] <= 2 and changes['p95Ms'] <= 5
            case = {'name': name, 'spatial': spatial, 'identicalOutput': True,
                    'median': medians, 'changePercent': changes,
                    'withinInvestigationThreshold': within, 'trials': trials}
            report['cases'].append(case)
            args.output.write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps({key: case[key] for key in ['name', 'spatial', 'changePercent', 'withinInvestigationThreshold']}), flush=True)
    report['withinInvestigationThreshold'] = all(case['withinInvestigationThreshold'] for case in report['cases'])
    report['completeComparison'] = len(report['cases']) == 4
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Report: {args.output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module-root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument('--baseline-source', type=pathlib.Path, required=True)
    parser.add_argument('--baseline-render-log', type=pathlib.Path, required=True)
    parser.add_argument('--work-dir', type=pathlib.Path, default=pathlib.Path('/tmp/tisynth-studio-bypass'))
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('/tmp/studio-bypass-benchmark.json'))
    parser.add_argument('--runs', type=int, default=10)
    parser.add_argument('--case', type=int, choices=[1, 2], help='Narrow investigation to one or 32 voices')
    parser.add_argument('--spatial', type=int, choices=[0, 1], help='Narrow investigation to stereo or binaural')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--build-only', action='store_true')
    group.add_argument('--run-only', action='store_true')
    args = parser.parse_args()
    if args.runs < 10:
        parser.error('At least ten alternating measured trials are required')
    if not args.run_only:
        build(args)
    if not args.build_only:
        measure(args)


if __name__ == '__main__':
    main()
