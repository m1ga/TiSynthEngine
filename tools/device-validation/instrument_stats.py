#!/usr/bin/env python3
"""Apply bounded telemetry only to an explicitly provided temporary source copy."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('snapshot', type=Path)
parser.add_argument('--ios-preferred-sample-rate', type=int, choices=[16000, 44100, 48000, 96000, 192000],
                    help='Diagnostic-only session preference before audio starts; actual rate must be verified')
args = parser.parse_args()
root = args.snapshot.resolve()
production = Path(__file__).resolve().parents[2]
if root == production or production in root.parents:
    raise SystemExit('Instrumentation must target a source snapshot outside the production repository')
p = root / 'shared/synth_stats.h'
original = p.read_text()
if 'SYNTH_PHYSICAL_STATS' in original:
    raise SystemExit('Snapshot already instrumented')
s = original.replace('typedef struct {\n  SynthStatsAtomic sequence,', '''typedef struct {
#ifdef SYNTH_PHYSICAL_STATS
  /* Epoch/count packing: one callback writer, lock-free scalar publication. */
  SynthStatsAtomic physicalEpoch, physicalBins[2048], physicalMisses, physicalUnderflow, physicalFreeze, physicalFrames;
#endif
  SynthStatsAtomic sequence,''', 1)
anchor = 'static inline bool synth_stats_begin(SynthStats *s) {'
helper = '''#ifdef SYNTH_PHYSICAL_STATS
#define SYNTH_PHYSICAL_BUCKETS 2048
static inline uint64_t synth_physical_add(SynthStatsAtomic *word,uint32_t epoch,uint32_t increment) {
  uint64_t old=__atomic_load_n(word,__ATOMIC_RELAXED);
  uint32_t count=(uint32_t)(old>>32)==epoch?(uint32_t)old:0;
  uint64_t next=((uint64_t)epoch<<32)|(uint64_t)(count+increment);
  __atomic_store_n(word,next,__ATOMIC_RELEASE);return count+increment;
}
static inline uint64_t synth_physical_increment(SynthStatsAtomic *word,uint32_t epoch) {return synth_physical_add(word,epoch,1);}
static inline void synth_physical_epoch(SynthStats *s) {
  uint32_t epoch=(uint32_t)synth_stats_load(&s->physicalEpoch)+1;
  synth_stats_store(&s->physicalEpoch,epoch?epoch:1);
  synth_stats_store(&s->physicalFreeze,0);
}
static inline void synth_physical_record(SynthStats *s,double load,int frames) {
  uint64_t freeze=synth_stats_load(&s->physicalFreeze);
  if(freeze){if(freeze==1)synth_stats_store(&s->physicalFreeze,2);return;}
  uint32_t epoch=(uint32_t)synth_stats_load(&s->physicalEpoch);
  synth_physical_add(&s->physicalFrames,epoch,(uint32_t)frames);
  int bin;
  if(isnan(load)||load<0){synth_physical_increment(&s->physicalUnderflow,epoch);bin=0;}
  else if(load>=2.047)bin=2047;
  else bin=(int)floor(load*1000);
  synth_physical_increment(&s->physicalBins[bin],epoch);
  if(load>1)synth_physical_increment(&s->physicalMisses,epoch);
}
static inline void synth_physical_freeze(SynthStats *s) {synth_stats_store(&s->physicalFreeze,1);}
static inline uint64_t synth_physical_word(const SynthStatsAtomic *word,uint32_t epoch) {
  uint64_t value=synth_stats_load(word);return (uint32_t)(value>>32)==epoch?(uint32_t)value:0;
}
static inline void synth_physical_read(const SynthStats *s,uint64_t *bins,uint64_t *misses,uint64_t *underflow) {
  uint32_t epoch=(uint32_t)synth_stats_load(&s->physicalEpoch);
  for(int i=0;i<2048;i++)bins[i]=synth_physical_word(&s->physicalBins[i],epoch);
  *misses=synth_physical_word(&s->physicalMisses,epoch);
  *underflow=synth_physical_word(&s->physicalUnderflow,epoch);
}
#endif
'''
s = s.replace(anchor, helper + anchor, 1)
s = s.replace('    s->seenConfig=c; s->seenReset=r;', '''#ifdef SYNTH_PHYSICAL_STATS
    synth_physical_epoch(s);
#endif
    s->seenConfig=c; s->seenReset=r;''', 1)
s = s.replace('    double load=seconds*rate/frames; s->peakLoad=fmax(s->peakLoad,load);', '''    double load=seconds*rate/frames; s->peakLoad=fmax(s->peakLoad,load);
#ifdef SYNTH_PHYSICAL_STATS
    synth_physical_record(s,load,frames);
#endif''', 1)
assert s != original
p.write_text(s)
module = root / 'ios/Classes/TiSynthengineModule.m'
if module.exists():
    m = ('#define SYNTH_PHYSICAL_STATS 1\n' + module.read_text()).replace('#import <mach/mach_time.h>', '#import <mach/mach_time.h>\n#import <mach/mach.h>\n#import <objc/runtime.h>')
    marker = '  return r;\n  }\n}\n- (NSNumber *)setStatsEnabled:'
    addition = '''#ifdef SYNTH_PHYSICAL_STATS
  id physicalOptions=[args isKindOfClass:[NSArray class]]&&[args count]==1?args[0]:args;
  if([physicalOptions isKindOfClass:[NSDictionary class]]&&[physicalOptions[@"physicalFreeze"] boolValue])synth_physical_freeze(&stats);
  uint64_t bins[2048],misses=0,underflow=0,total=0;c
  synth_physical_read(&stats,bins,&misses,&underflow);
  BOOL histogramFrozen=synth_stats_load(&stats.physicalFreeze)==2;
  NSMutableArray *hist=histogramFrozen?[NSMutableArray array]:nil;for(int i=0;i<2048;i++)if(bins[i]){total+=bins[i];if(hist)[hist addObject:@[@(i),@(bins[i])]];}
  uint64_t rank=(total*95+99)/100,sum=0;int percentile=0;
  for(int i=0;i<2048;i++){sum+=bins[i];if(sum>=rank){percentile=i;break;}}
  r[@"physicalTelemetry"]=@{@"callbacks":@(total),@"renderedFrames":@(synth_physical_word(&stats.physicalFrames,(uint32_t)synth_stats_load(&stats.physicalEpoch))),@"histogram":hist?:@[],@"p95Lower":@(percentile/1000.0),@"p95Upper":percentile<2047?(id)@((percentile+1)/1000.0):[NSNull null],@"deadlineMisses":@(misses),@"invalidNegativeOrNaN":@(underflow),@"overflow":@(bins[2047]),@"frozen":@(synth_stats_load(&stats.physicalFreeze)==2)};
  if([physicalOptions isKindOfClass:[NSDictionary class]]&&[physicalOptions[@"physicalRecovery"] boolValue]&&audioEngine) {
    [audioEngine pause];r[@"physicalRecoveryRequested"]=@YES;
    [self resumeAudioEngineAfterRouteChange:@"diagnostic controlled recovery"];
  }
  r[@"physicalEngineRunning"]=@(audioEngine&&[audioEngine isRunning]);
  r[@"physicalTransportIdentity"]=[NSString stringWithFormat:@"%p",sequence];
  r[@"physicalEffectsAllocatedBytes"]=@(effects.memoryBytes);
  if([physicalOptions isKindOfClass:[NSDictionary class]]&&[physicalOptions[@"physicalMemory"] boolValue]) {
    task_vm_info_data_t info={0};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
    kern_return_t memoryResult=task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&info,&count);
    r[@"physicalProcessMemory"]=@{@"kernelResult":@(memoryResult),@"physFootprint":@(info.phys_footprint),@"physFootprintPeak":@(info.ledger_phys_footprint_peak),@"thermalState":@([NSProcessInfo processInfo].thermalState),@"residentSize":@(info.resident_size),@"residentPeak":@(info.resident_size_peak),@"virtualSize":@(info.virtual_size),@"nativeModuleIvarBytes":@(class_getInstanceSize([self class])),@"telemetryStructBytes":@(sizeof(SynthStats))};
    AVAudioSession *physicalSession=[AVAudioSession sharedInstance];NSMutableArray *ports=[NSMutableArray array];
    for(AVAudioSessionPortDescription *port in physicalSession.currentRoute.outputs)[ports addObject:@{@"name":port.portName,@"type":port.portType}];
    r[@"physicalAudioSession"]=@{@"sessionSampleRate":@(physicalSession.sampleRate),@"ioBufferDuration":@(physicalSession.IOBufferDuration),@"outputLatency":@(physicalSession.outputLatency),@"outputNodeSampleRate":audioEngine?(id)@([audioEngine.outputNode outputFormatForBus:0].sampleRate):[NSNull null],@"sourceNodeSampleRate":sourceNode?(id)@([sourceNode outputFormatForBus:0].sampleRate):[NSNull null],@"category":physicalSession.category,@"mode":physicalSession.mode,@"systemOutputVolume":@(physicalSession.outputVolume),@"ports":ports};
  }
#endif
'''.replace('total=0;c', 'total=0;')
    if args.ios_preferred_sample_rate is not None:
        addition += '''#ifdef SYNTH_PHYSICAL_STATS
  if(!audioEngine) {
    NSError *physicalRateError=nil;
    BOOL physicalRateAccepted=[[AVAudioSession sharedInstance] setPreferredSampleRate:%d error:&physicalRateError];
    r[@"physicalPreferredSampleRateRequest"]=@{@"requested":@(%d),@"accepted":@(physicalRateAccepted),@"error":physicalRateError?(id)physicalRateError.localizedDescription:[NSNull null]};
  }
#endif
''' % (args.ios_preferred_sample_rate, args.ios_preferred_sample_rate)
    assert marker in m
    m = m.replace(marker, addition + marker, 1)
    module.write_text(m)
    xc = root / 'ios/module.xcconfig'
    xc.write_text(xc.read_text() + '\nGCC_PREPROCESSOR_DEFINITIONS = $(inherited) SYNTH_PHYSICAL_STATS=1\n')
manifest = {'productionStatsSha256': hashlib.sha256(original.encode()).hexdigest(),
            'instrumentedStatsSha256': hashlib.sha256(s.encode()).hexdigest(),
            'compileFlag': 'SYNTH_PHYSICAL_STATS=1', 'buckets': 2048, 'binWidthLoad': 0.001,
            'overflowLower': 2.047, 'deadlineMissThreshold': 'render seconds > callback frames / sample rate',
            'reset': 'epoch tags; O(1) callback reset', 'callbackMemory': 'fixed atomic scalar arrays; no allocation, lock, wait or scan',
            'freeze': 'control request acknowledged at next callback; stable histogram thereafter',
            'iosPreferredSampleRate': args.ios_preferred_sample_rate,
            'preferredRateScope': 'Diagnostic getStats before audioEngine exists; no buffer request; verify actual source/session rates'}
(root/'physical-instrumentation.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(json.dumps(manifest))
