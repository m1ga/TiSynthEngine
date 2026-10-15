// Same native host engine and compiler flags as spatial_benchmark_host.cpp.
#define main original_engine_main
#include "engine_render_host.cpp"
#undef main
#include <cstdlib>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
int main(int argc,char **argv) {
  #ifdef __APPLE__
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
  #endif
  int scenario=argc>1?atoi(argv[1]):0;
  bool spatial=argc>2 && atoi(argv[2]);
  Rig rig(32,1);
#ifndef STATS_BASELINE
  synth_stats_enable(&rig.e().stats_,std::getenv("SYNTH_STATS_FULL")!=nullptr);
#endif
  std::vector<double> times; double total=0; uint64_t hash=1469598103934665603ULL;
  constexpr int frames=48000*20;
  int nextPoll=0;
  for(int frame=0;frame<frames;frame+=256) {
    auto begin=std::chrono::steady_clock::now();
    int voices= scenario==2||scenario==4?32:1;
    bool event=scenario==1||scenario==2 ? frame==0 : scenario==3 ? frame%2048==0 : scenario==4 ? frame%24064==0 : scenario==5 ? frame%4864==0 : scenario==6 ? frame%1024==0 : false;
    if(event) {
      std::vector<Command> chord;
      for(int i=0;i<voices;i++) {
        int duration=scenario==3?3000:scenario==6?1:scenario==1||scenario==2?20000:120;
        Command c=note(rig,220+i*11,duration,0,0,0.5,0);
        c.spatial=spatial; c.spatialX=0.6; c.spatialY=-0.4;
        chord.push_back(c);
      }
      rig.e().enqueueChord(chord);
    }
    float output[512]; int count=std::min(256,frames-frame);
    rig.e().onAudioReady(nullptr,output,count);
#ifndef STATS_BASELINE
    if(frame>=nextPoll) { double v[SYNTH_STATS_COUNT]; rig.e().readStats(v); rig.e().pendingCommands(); nextPoll+=9600; }
#else
    (void)nextPoll;
#endif
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    total+=ms; times.push_back(ms);
    for(int i=0;i<count*2;i++) { uint32_t bits; memcpy(&bits,&output[i],4); hash=(hash^bits)*1099511628211ULL; }
  }
  std::sort(times.begin(),times.end());
  printf("{\"renderMs\":%.9f,\"p95Ms\":%.9f,\"hash\":\"%llu\"}\n",total,times[times.size()*95/100],(unsigned long long)hash);
}
