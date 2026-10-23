// One harness can compile against archived and current engines. The source
// scores and deterministic noise seeds are identical; compare stdout exactly.
#define main original_render_main
#include "engine_render_host.cpp"
#undef main
int main() {
 for(int rate:{16000,44100,48000,96000,192000}) {
  for(int kind=0;kind<6;kind++) {
   Rig r(8,1);r.e().renderSampleRate_=rate;r.e().reportedSampleRate_.store(rate);r.e().initializeLimiter();
   auto c=note(r,440,400,2,80,.2,kind);
   c.noiseRate=1200;c.lowPassHz=3000;c.pulseWidth=.2;
   if(kind==5){c.wavetableSize=4;c.wavetable[0]=-1;c.wavetable[1]=-.2;c.wavetable[2]=1;c.wavetable[3]=.3;}
   r.e().enqueue(c);r.settle();auto samples=r.render(rate/2);
   printf("%d %d %016llx\n",rate,kind,(unsigned long long)fnv(samples));
  }
 }
}
