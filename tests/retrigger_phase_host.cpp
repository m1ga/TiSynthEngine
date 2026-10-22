// Public keyboard retrigger regression; same Oboe/JNI host setup as
// engine_render_host.cpp. From the module root, after setting OBOE and JH:
// clang++ -std=c++17 -O2 -Wall -I"$OBOE" -I"$JH/include" -I"$JH/include/darwin" \
//   -DHAS_EXPRESSIVE -DENGINE_CPP='"../android/jni/SynthOboeEngine.cpp"' \
//   -Wl,-undefined,dynamic_lookup tests/retrigger_phase_host.cpp \
//   -o /tmp/retrigger_phase_host && /tmp/retrigger_phase_host
// Only compatible releasing positive-ID held notes donate. Active same-ID
// replacements, timed calls, transport and two-LFO modulation keep phase reset.
#define main legacy_main
#include "engine_render_host.cpp"
#undef main
#include <cassert>
static Command repeated(Rig&r,int id,int wave=0,double frequency=261.6255653005986) {
 auto c=note(r,frequency,100,180,900,.130621573636,wave);c.type=SYNTH_CMD_HELD_ON;c.voiceId=id;
 c.decay=8640;c.sustain=.7;c.pulseWidth=.3;c.filterType=SYNTH_FILTER_LOWPASS;c.filterHz=2400;c.filterQ=.7071;return c;
}
static Voice *voiceId(Rig&r,int id){for(auto &v:r.e().voices_)if(v.active&&v.held.voiceId==id)return &v;return nullptr;}
static void startHeld(Rig&r,Command c){assert(!r.e().enqueue(c));r.e().processCommands();}
static void releaseHeld(Rig&r,int id){Command c;c.type=SYNTH_CMD_HELD_OFF;c.voiceId=id;assert(!r.e().enqueue(c));r.e().processCommands();}
static double acRms(const std::vector<float>&x,int from,int count){double sum=0,squares=0;for(int i=from;i<from+count;i++){double v=x[i*2];sum+=v;squares+=v*v;}return sqrt(std::max(0.,squares/count-(sum/count)*(sum/count)));}
static void signalRegression(int wave,int spacing) {
 Rig r(32,1);r.e().setVolume(2);r.e().enableStats(true);r.render(4800);std::vector<float> out;
 for(int i=0;i<3;i++){startHeld(r,repeated(r,i+1,wave));auto a=r.render(3360);out.insert(out.end(),a.begin(),a.end());releaseHeld(r,i+1);auto b=r.render(spacing*48-3360);out.insert(out.end(),b.begin(),b.end());}
 auto tail=r.render(48000);out.insert(out.end(),tail.begin(),tail.end());
 double first=acRms(out,3840,4800),second=acRms(out,spacing*48+3840,4800),third=acRms(out,spacing*96+3840,4800);
 printf("retrigger wave%d spacing%d AC ratios second %.6f third %.6f reduction %.6fdB\n",wave,spacing,second/first,third/first,r.e().stats_.values[ST_MAX_REDUCTION]);
 assert(second>=first*.95&&third>=first*.95);assert(r.e().stats_.values[ST_MAX_REDUCTION]<1e-9);
}
int main() {
 // RED before the fix: -9.45dB on the second sine and -4.74dB on the third PWM.
 signalRegression(0,300);signalRegression(1,200);
 for(int count:{1,32}) {
  Rig r(count,1);auto c=repeated(r,1);startHeld(r,c);r.render(3360);releaseHeld(r,1);r.render(11040);
  auto *old=voiceId(r,1);assert(old);double phase=old->phase;auto filter=old->filter;uint64_t order=old->held.order;
  c.voiceId=2;startHeld(r,c);auto *next=voiceId(r,2);assert(next&&phase>.1&&next->phase==phase);
  assert(next->filter.ic1==filter.ic1&&next->filter.ic2==filter.ic2);
  assert(!next->held.releasing&&next->envelope==0&&next->held.order>order&&next->held.voiceId==2);
 }
 // Exact MIDI values must match without sample-rate roundtrip reconstruction.
 for(int rate:{16000,44100,48000,96000,192000})for(int midi:{24,60,83,108}) {
  Rig r(2);r.e().renderSampleRate_=rate;
  auto c=repeated(r,1,0,440*std::exp2((midi-69)/12.));
  startHeld(r,c);r.render(1234);releaseHeld(r,1);double phase=voiceId(r,1)->phase;
  c.voiceId=2;startHeld(r,c);assert(phase!=0&&voiceId(r,2)->phase==phase);
 }
 // Active same-ID replacement retains its previous policy; donors release only.
 {Rig r(2);auto c=repeated(r,1);startHeld(r,c);r.render(1000);startHeld(r,c);assert(voiceId(r,1)->phase==0);}
 // Distinct held unisons are still independent attacks and occupy two voices.
 {Rig r(32);auto c=repeated(r,1);startHeld(r,c);r.render(1000);c.voiceId=2;startHeld(r,c);assert(voiceId(r,1)&&voiceId(r,2)&&voiceId(r,2)->phase==0);}
 // Newest compatible releasing donor wins, even when an older one also matches.
 {Rig r(32);auto c=repeated(r,1);startHeld(r,c);r.render(1000);c.voiceId=2;startHeld(r,c);r.render(1000);releaseHeld(r,1);releaseHeld(r,2);double phase=voiceId(r,2)->phase;c.voiceId=3;startHeld(r,c);assert(voiceId(r,3)->phase==phase);}
 // A changed PWM, a changing pitch or a different pitch must not donate.
 for(int mode:{0,1,2}){Rig r(32);auto c=repeated(r,1,1);startHeld(r,c);r.render(1000);releaseHeld(r,1);c.voiceId=2;
  if(mode==0)c.pulseWidth=.4;else if(mode==1){Command u;u.type=SYNTH_CMD_HELD_UPDATE;u.voiceId=1;u.liveMask=1u<<LIVE_FREQUENCY;u.liveValues[LIVE_FREQUENCY]=300;assert(!r.e().enqueue(u));r.e().processCommands();c.frequency=300;}else c.frequency=300;
  startHeld(r,c);assert(voiceId(r,2)->phase==0);
 }
 // Copy both constant-FM carrier/modulator and stable vibrato phases.
 {Rig r(32);auto c=repeated(r,1);c.fmDepth=2;c.fmRatio=1.5;c.lfoFreq=5;c.lfoDepth=3;startHeld(r,c);r.render(1000);releaseHeld(r,1);auto old=*voiceId(r,1);c.voiceId=2;startHeld(r,c);auto *next=voiceId(r,2);assert(next->phase==old.phase&&next->modPhase==old.modPhase&&next->lfoPhase==old.lfoPhase);}
 // Negative transport IDs and two-LFO modulation retain their previous policy.
 for(bool transport:{false,true}){Rig r(32);auto c=repeated(r,transport?-1:1);if(!transport){c.hasModulations=true;c.modulations.lfo[0].enabled=1;c.modulations.lfo[0].depth=.2;}startHeld(r,c);r.render(1000);releaseHeld(r,c.voiceId);c.voiceId=2;startHeld(r,c);assert(voiceId(r,2)->phase==0);}
 puts("keyboard retrigger phase regression passed");
}
