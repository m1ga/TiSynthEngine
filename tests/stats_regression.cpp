#include <cassert>
#include <cmath>
#include <thread>
#include "../shared/synth_stats.h"
int main() {
  SynthStats s{}; synth_stats_init(&s);
  double a[SYNTH_STATS_COUNT]; synth_stats_read(&s,a);
  assert(a[ST_ACTIVE]==0 && std::isnan(a[ST_LOAD]));
  synth_stats_begin(&s); synth_stats_voice_start(&s,false,false,true);
  synth_stats_voice_start(&s,true,true,false); synth_stats_tail_start(&s,false);
  synth_stats_end(&s,256,48000,0);
  synth_stats_read(&s,a);
  assert(a[ST_ACTIVE]==1 && a[ST_PEAK_ACTIVE]==1 && a[ST_SPATIAL]==0 && a[ST_STEALS]==1 && a[ST_TAILS]==1);
  synth_stats_reset(&s); synth_stats_read(&s,a);
  assert(a[ST_STEALS]==0 && a[ST_PEAK_ACTIVE]==1 && a[ST_ACTIVE]==1);
  synth_stats_enable(&s,true); assert(synth_stats_begin(&s));
  for(int i=0;i<9600;i++) synth_stats_sample(&s,0.5f,0.25f,0.5,48000);
  synth_stats_end(&s,9600,48000,0.3); synth_stats_read(&s,a);
  assert(std::abs(a[ST_LOAD]-1.5)<1e-12);
  assert(std::abs(a[ST_PEAK_L]+6.020599913)<1e-8 && a[ST_RMS_L]==a[ST_PEAK_L]);
  assert(std::abs(a[ST_REDUCTION]-6.020599913)<1e-8);
  synth_stats_pre_limiter(&s, 2.0, -1.0);
  synth_stats_end(&s,1,48000,0); synth_stats_read(&s,a);
  assert(std::abs(a[ST_PRE_LIMITER_PEAK]-6.020599913)<1e-8);
  for(int i=0;i<9600;i++) synth_stats_sample(&s,0,0,1,48000);
  synth_stats_end(&s,9600,48000,0); synth_stats_read(&s,a);
  assert(a[ST_REDUCTION]==0 && std::abs(a[ST_MAX_REDUCTION]-6.020599913)<1e-8);
  synth_stats_reset(&s); synth_stats_begin(&s);
  for(int i=0;i<9599;i++) synth_stats_sample(&s,0,0,1,48000);
  synth_stats_end(&s,9599,48000,0.001); synth_stats_read(&s,a);
  assert(std::isnan(a[ST_PEAK_L]));
  synth_stats_sample(&s,0,0,1,48000); synth_stats_end(&s,1,48000,0.00001);
  synth_stats_read(&s,a); assert(a[ST_PEAK_L]==-120 && a[ST_RMS_R]==-120 && a[ST_REDUCTION]==0);
  synth_stats_enable(&s,false); synth_stats_read(&s,a); assert(std::isnan(a[ST_LOAD]));
  synth_stats_begin(&s); synth_stats_voice_end(&s,false); synth_stats_tail_end(&s);
  synth_stats_end(&s,256,48000,0); synth_stats_read(&s,a); assert(a[ST_ACTIVE]==0 && a[ST_TAILS]==0 && a[ST_PEAK_ACTIVE]==1);
  std::thread reader([&] { for(int i=0;i<100000;i++) { double v[SYNTH_STATS_COUNT]; synth_stats_read(&s,v); assert(v[ST_ACTIVE]>=0 && v[ST_ACTIVE]<=1); }});
  for(int i=0;i<10000;i++) { synth_stats_begin(&s); synth_stats_voice_start(&s,false,false,false); synth_stats_end(&s,256,48000,0); synth_stats_begin(&s); synth_stats_voice_end(&s,false); synth_stats_end(&s,256,48000,0); }
  reader.join();
}
