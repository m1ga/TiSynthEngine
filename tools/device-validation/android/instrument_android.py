#!/usr/bin/env python3
"""Add bounded Android callback telemetry to an external source snapshot only.

The production module API and DSP remain untouched. Reuses the shared diagnostic
histogram patch, then adds a snapshot-only getter and callback-end CPU samples.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('snapshot', type=Path)
parser.add_argument('--thread-cpu', action='store_true', help='Add paired CLOCK_THREAD_CPUTIME_ID callback histograms')
args = parser.parse_args()
root = args.snapshot.resolve()
production = Path(__file__).resolve().parents[3]
if root == production or production in root.parents:
    raise SystemExit('Instrumentation requires a source snapshot outside the production repository')
cpp = root / 'android/jni/SynthOboeEngine.cpp'
original_cpp = cpp.read_text()
if 'SYNTH_PHYSICAL_STATS' in original_cpp:
    raise SystemExit('Android snapshot already instrumented')
subprocess.run([sys.executable, str(production / 'tools/device-validation/instrument_stats.py'), str(root)], check=True)
header = root / 'shared/synth_stats.h'
s = header.read_text()
s = s.replace('#include <math.h>', '#include <math.h>\n#ifdef __ANDROID__\n#include <sched.h>\n#endif', 1)
s = s.replace('physicalBins[2048], physicalMisses', 'physicalBins[2048], physicalCpus[64], physicalCpuUnknown, physicalMisses', 1)
anchor = '  synth_physical_increment(&s->physicalBins[bin],epoch);'
assert anchor in s
s = s.replace(anchor, anchor + '''
#ifdef __ANDROID__
  int cpu=sched_getcpu();
  if(cpu>=0&&cpu<64)synth_physical_increment(&s->physicalCpus[cpu],epoch);
  else synth_physical_increment(&s->physicalCpuUnknown,epoch);
#endif''', 1)
header.write_text(s)
s = '#define SYNTH_PHYSICAL_STATS 1\n' + original_cpp
anchor = '  void readStats(double *out) {'
assert anchor in s
s = s.replace(anchor, '''  void readPhysicalStats(double *out,bool freeze) {
    if(freeze)synth_physical_freeze(&stats_);
    uint64_t bins[2048],misses=0,invalid=0,total=0;
    synth_physical_read(&stats_,bins,&misses,&invalid);
    for(int i=0;i<2048;i++){out[6+i]=(double)bins[i];total+=bins[i];}
    out[0]=(double)total;out[1]=(double)misses;out[2]=(double)invalid;
    out[3]=synth_stats_load(&stats_.physicalFreeze)==2;
    uint32_t epoch=(uint32_t)synth_stats_load(&stats_.physicalEpoch);out[4]=epoch;
    out[5]=(double)synth_physical_word(&stats_.physicalCpuUnknown,epoch);
    for(int i=0;i<64;i++)out[2054+i]=(double)synth_physical_word(&stats_.physicalCpus[i],epoch);
  }
''' + anchor, 1)
s += '''
extern "C" JNIEXPORT jdoubleArray JNICALL
Java_ti_synthengine_SynthThread_nativePhysicalStats(JNIEnv *env,jclass,jlong handle,jboolean freeze) {
  double values[2118]={};fromHandle(handle)->readPhysicalStats(values,freeze);
  auto result=env->NewDoubleArray(2118);if(result)env->SetDoubleArrayRegion(result,0,2118,values);return result;
}
'''
cpp.write_text(s)
p = root / 'android/src/ti/synthengine/SynthThread.java'
s = p.read_text()
anchor = '    public synchronized double[] getStats()'
assert anchor in s
s = s.replace(anchor, '''    public synchronized double[] physicalStats(boolean freeze) {return nativeHandle==0?null:nativePhysicalStats(nativeHandle,freeze);}
    private static native double[] nativePhysicalStats(long handle,boolean freeze);
''' + anchor, 1)
p.write_text(s)
p = root / 'android/src/ti/synthengine/TiSynthEngineModule.java'
s = p.read_text()
anchor = '    @Kroll.method\n    public synchronized KrollDict getStats()'
assert anchor in s
s = s.replace(anchor, '''    @Kroll.method
    public synchronized KrollDict getPhysicalStats(KrollDict options) {
        double[] p=synthThread==null?null:synthThread.physicalStats(options!=null&&Boolean.TRUE.equals(options.get("freeze")));
        if(p==null)return null;
        long total=(long)p[0],rank=(total*95+99)/100,sum=0;int percentile=0;
        java.util.ArrayList<Object> histogram=new java.util.ArrayList<>(),cpus=new java.util.ArrayList<>();
        for(int i=0;i<2048;i++)if(p[6+i]>0)histogram.add(new Object[]{i,(long)p[6+i]});
        for(int i=0;i<2048;i++){sum+=(long)p[6+i];if(sum>=rank){percentile=i;break;}}
        for(int i=0;i<64;i++)if(p[2054+i]>0)cpus.add(new Object[]{i,(long)p[2054+i]});
        KrollDict r=new KrollDict();r.put("callbacks",total);r.put("deadlineMisses",(long)p[1]);r.put("invalidNegativeOrNaN",(long)p[2]);
        r.put("frozen",p[3]!=0);r.put("epoch",(long)p[4]);r.put("cpuUnknown",(long)p[5]);
        r.put("p95Lower",percentile/1000.0);r.put("p95Upper",percentile==2047?null:(percentile+1)/1000.0);
        r.put("overflow",(long)p[2053]);r.put("histogram",histogram.toArray());r.put("callbackEndCpuSamples",cpus.toArray());return r;
    }
''' + anchor, 1)
p.write_text(s)
if args.thread_cpu:
    # Deliberately kept in an external snapshot: timestamp syscalls and bins are
    # diagnostic overhead, never part of the production module contract.
    h = header.read_text().replace('physicalBins[2048], physicalCpus', 'physicalBins[2048], physicalThreadBins[2048], physicalThreadMisses, physicalCpus')
    header.write_text(h)
    c = cpp.read_text()
    anchor = '    processCommands();\n    synth_transport_begin_block'
    assert anchor in c
    c = c.replace(anchor, '    struct timespec cpuBegin{}; if(measure)clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpuBegin);\n' + anchor, 1)
    anchor = '    synth_stats_end(&stats_, frames, renderSampleRate_, measure ? std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count() : 0);'
    assert anchor in c
    c = c.replace(anchor, '''    const double wallSeconds=measure?std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count():0;
    struct timespec cpuEnd{};if(measure)clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpuEnd);
    if(measure&&!synth_stats_load(&stats_.physicalFreeze)){
      double cpuSeconds=(cpuEnd.tv_sec-cpuBegin.tv_sec)+(cpuEnd.tv_nsec-cpuBegin.tv_nsec)*1e-9;
      double load=cpuSeconds*renderSampleRate_/frames;
      int bin=load>=2.047?2047:(int)floor(load*1000);
      uint32_t epoch=(uint32_t)synth_stats_load(&stats_.physicalEpoch);
      synth_physical_increment(&stats_.physicalThreadBins[bin<0?0:bin],epoch);
      if(load>1)synth_physical_increment(&stats_.physicalThreadMisses,epoch);
    }
    synth_stats_end(&stats_, frames, renderSampleRate_, wallSeconds);''', 1)
    anchor = '    for(int i=0;i<64;i++)out[2054+i]=(double)synth_physical_word(&stats_.physicalCpus[i],epoch);'
    assert anchor in c
    c = c.replace(anchor, anchor + '\n    for(int i=0;i<2048;i++)out[2118+i]=(double)synth_physical_word(&stats_.physicalThreadBins[i],epoch);\n    out[4166]=(double)synth_physical_word(&stats_.physicalThreadMisses,epoch);', 1)
    c = c.replace('double values[2118]', 'double values[4167]').replace('NewDoubleArray(2118)', 'NewDoubleArray(4167)').replace('SetDoubleArrayRegion(result,0,2118,values)', 'SetDoubleArrayRegion(result,0,4167,values)')
    cpp.write_text(c)
    j = p.read_text()
    anchor = 'r.put("histogram",histogram.toArray());r.put("callbackEndCpuSamples",cpus.toArray());return r;'
    assert anchor in j
    j = j.replace(anchor, '''r.put("histogram",histogram.toArray());r.put("callbackEndCpuSamples",cpus.toArray());
        java.util.ArrayList<Object> cpuHistogram=new java.util.ArrayList<>();long cpuTotal=0,cpuSum=0;int cpuP95=0;
        for(int i=0;i<2048;i++){cpuTotal+=(long)p[2118+i];if(p[2118+i]>0)cpuHistogram.add(new Object[]{i,(long)p[2118+i]});}
        long cpuRank=(cpuTotal*95+99)/100;for(int i=0;i<2048;i++){cpuSum+=(long)p[2118+i];if(cpuSum>=cpuRank){cpuP95=i;break;}}
        r.put("threadCpuHistogram",cpuHistogram.toArray());r.put("threadCpuCallbacks",cpuTotal);r.put("threadCpuDeadlineMisses",(long)p[4166]);r.put("threadCpuP95Lower",cpuP95/1000.0);r.put("threadCpuP95Upper",cpuP95==2047?null:(cpuP95+1)/1000.0);return r;''', 1)
    p.write_text(j)

proof = {'productionCppSha256': hashlib.sha256(original_cpp.encode()).hexdigest(), 'snapshotCppSha256': hashlib.sha256(cpp.read_bytes()).hexdigest(), 'histogramBins': 2048, 'binWidthLoad': .001, 'overflowStarts': 2.047, 'cpuBins': 64, 'freezeAcknowledgedBeforeStableRead': True, 'productionApiChanged': False}
proof['threadCpuTiming'] = args.thread_cpu
proof['snapshotHeaderSha256'] = {h.name: hashlib.sha256(h.read_bytes()).hexdigest() for h in (root / 'shared').glob('*.h')}
(root / 'physical-android-instrumentation.json').write_text(json.dumps(proof, indent=2) + '\n')
print(json.dumps(proof, indent=2))
