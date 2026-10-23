#!/usr/bin/env python3
"""Summarize a completed isolated iPad run without treating sampled loads as p95."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('report', type=Path)
parser.add_argument('--evidence-root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
p = json.loads(args.report.read_text())
if p.get('stage') != 'completed':
    raise SystemExit('A completed timed load and memory/restart phase are required')
t = p['finalTelemetry']
hist = t['histogram']
assert t['frozen'] and sum(count for _, count in hist) == t['callbacks']
rank = (t['callbacks'] * 95 + 99) // 100
cumulative = 0
for index, count in hist:
    cumulative += count
    if cumulative >= rank:
        p95_lower = index / 1000
        p95_upper = (index + 1) / 1000 if index < 2047 else None
        break
assert p95_upper == t['p95Upper']
rate = p['sampleRate']
audio_seconds = t['renderedFrames'] / rate
observations = p['observations']
minimum_voices = min(x['stats']['activeVoices'] for x in observations)
minimum_spatial = min(x['stats']['spatialVoices'] for x in observations)
maximum_rejections = max(x['stats']['rejectedRequestCount'] for x in observations)
maximum_publication_rejections = max(x['stats']['rejectedPublicationCount'] for x in observations)
maximum_priority_drops = max(x['stats']['priorityDropCount'] for x in observations)
valid_sequences = all(len(x['positions']) == 4 and all(s and s['playing'] for s in x['positions']) for x in observations)
render_pass = (audio_seconds >= 600 and p['measuredDurationMs'] >= 600000
               and p95_upper is not None and p95_upper <= .5
               and t['deadlineMisses'] == 0 and t['invalidNegativeOrNaN'] == 0
               and minimum_voices == 32 and minimum_spatial == 32 and valid_sequences
               and maximum_rejections == maximum_publication_rejections == maximum_priority_drops == 0
               and not p.get('backgroundedAt'))
base = p['baselineProcessMemory']
pressure = p['memoryPressure']
memories = [base, p['feasibility']['physicalProcessMemory'], p['fullLoadProcessMemory']]
memories += [pressure[key] for key in ['preShutdownMemory', 'postPressureMemory', 'restartMemory', 'finalShutdownMemory']]
assert all(x['kernelResult'] == 0 for x in memories)
peak = max(x['physFootprintPeak'] for x in memories)
increment = max(0, peak - base['physFootprint'])
accounting = [json.loads(line) for line in (args.evidence_root/'native-memory-accounting.jsonl').read_text().splitlines()]
actual = next(x for x in accounting if x['sampleRate'] == rate)
high = next(x for x in accounting if x['sampleRate'] == 192000)
telemetry_extra = base['telemetryStructBytes'] - 392
production_ivars = base['nativeModuleIvarBytes'] - telemetry_extra
buffer_delta = high['totalHeapOutsideModuleIvars'] - actual['totalHeapOutsideModuleIvars']
projected = increment + production_ivars + buffer_delta
result = {
    'status': 'completed', 'deviceModel': p['model'], 'osVersion': p['osVersion'],
    'appId': p['appId'], 'renderTimingPassed': render_pass, 'acceptanceComplete': False,
    'measurementStartedAt': p['measurementStartedAt'], 'measurementFinishedAt': p['measurementFinishedAt'],
    'wallDurationMs': p['measuredDurationMs'], 'renderedAudioSeconds': audio_seconds,
    'callbacks': t['callbacks'], 'renderedFrames': t['renderedFrames'],
    'sourceRendererSampleRate': rate, 'audioSession': p['fullLoadAudioSession'],
    'renderLoadP95': {'lowerInclusive': p95_lower, 'upperExclusive': p95_upper, 'basis': 'all measured callbacks; frozen native histogram; nearest rank'},
    'renderLoadMaximum': p['accumulatedRenderLoadMaximum'],
    'measuredRenderDeadlineMisses': t['deadlineMisses'],
    'deadlineMissThreshold': 'render seconds > callback frames / source rate',
    'iOSOutputUnderruns': p['outputUnderrunCount'],
    'minimumObservedVoices': minimum_voices, 'minimumObservedSpatialVoices': minimum_spatial,
    'fourSequencesContinuouslyObservedPlaying': valid_sequences,
    'rejectedRequests': maximum_rejections, 'rejectedPublications': maximum_publication_rejections,
    'priorityDrops': maximum_priority_drops, 'backgroundedAt': p.get('backgroundedAt'),
    'output': {'finalPeakDbFS': p['latest']['outputPeakDbFS'], 'maximumLimiterReductionDb': p['latest']['maxLimiterReductionDb']},
    'memory': {'baselineAfterRequireBeforeStart': base, 'fullLoad': p['fullLoadProcessMemory'],
        'pressure': pressure, 'lifetimePhysicalFootprintPeak': peak,
        'processPeakMinusBaselineCurrentUpper': increment,
        'diagnosticStatsExtraBytes': telemetry_extra, 'productionNativeIvarBytes': production_ivars,
        'countedNativeBytesAtActualRate': production_ivars + actual['totalHeapOutsideModuleIvars'],
        'countedNativeBytes192k': production_ivars + high['totalHeapOutsideModuleIvars'],
        'bufferDeltaActualTo192k': buffer_delta,
        'conservative192kProjectionBytes': projected,
        'conservative192kProjectionWithin32MiB': projected <= 32*1024*1024,
        'projectionScope': 'Process lifetime peak minus post-require baseline current footprint, plus production module ivars (potential double count of lazily resident pages) and known native buffer delta. Includes runtime/bridge/diagnostic effects. This is an estimate; 192 kHz hardware/driver behavior was not measured.'},
    'rawReport': str(args.report), 'rawEvidenceRoot': str(args.evidence_root),
    'listening': {'humanAcceptance': 'pending', 'demonstration': 'Open TiSynthPhysical and tap Listen: quiet native phrase after completion. No listening result is inferred from counters.'},
    'instrumentation': json.loads((args.evidence_root/'instrumented-source-hashes.json').read_text()),
    'telemetryOverhead': json.loads((args.evidence_root/'telemetry-overhead.json').read_text()),
}
args.output.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps({k: result[k] for k in ['renderTimingPassed', 'wallDurationMs', 'renderedAudioSeconds', 'callbacks', 'renderLoadP95', 'renderLoadMaximum', 'measuredRenderDeadlineMisses']}))
