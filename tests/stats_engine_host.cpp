#define main original_engine_main
#include "engine_render_host.cpp"
#undef main
int main() {
  Rig a(2), b(2);
  synth_stats_enable(&b.e().stats_,true);
  for(int spatial=0;spatial<2;spatial++) {
    Command c=note(a,440,100,1,10,1,0); c.spatial=spatial;
    a.e().enqueue(c); b.e().enqueue(c);
    CHECK(a.render(1000)==b.render(1000),"diagnostics preserve every output sample");
    synth_stats_reset(&b.e().stats_);
    CHECK(a.render(5000)==b.render(5000),"reset during playback preserves every output sample");
  }
  Command c=note(a,440,1000,1,10,1,0); c.spatial=true;
  a.e().enqueue(c); a.e().enqueue(c); a.e().enqueue(c);
  a.render(1); double v[SYNTH_STATS_COUNT]; synth_stats_read(&a.e().stats_,v);
  CHECK(v[ST_ACTIVE]==2 && v[ST_PEAK_ACTIVE]==2 && v[ST_STEALS]==1 && v[ST_TAILS]==1 && v[ST_SPATIAL]==2,"occupancy, steal, tail and spatial counts");
  synth_stats_reset(&a.e().stats_); a.render(1); synth_stats_read(&a.e().stats_,v);
  CHECK(v[ST_ACTIVE]==2 && v[ST_STEALS]==0 && v[ST_PEAK_ACTIVE]==2,"reset preserves playback");
  a.render(50000); synth_stats_read(&a.e().stats_,v);
  CHECK(v[ST_ACTIVE]==0 && v[ST_TAILS]==0 && v[ST_PEAK_ACTIVE]==2,"peaks survive polling gaps and binaural drain");
  for(int i=0;i<127;i++) CHECK(!a.e().enqueue(c),"queue capacity");
  CHECK(a.e().enqueue(c),"queue overflow");
  a.e().close(); synth_stats_read(&a.e().stats_,v);
  CHECK(v[ST_ACTIVE]==0 && v[ST_TAILS]==0 && std::isnan(v[ST_LOAD]) && a.e().pendingCommands()==0,"shutdown clears occupancy, commands and measurements");
  return failures?1:0;
}
