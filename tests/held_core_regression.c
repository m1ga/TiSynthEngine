#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../shared/synth_held.h"
int main(void) {
  SynthHeld h = {0};
  h.voiceId = 1;
  for (int age = 0; age < 16000; age++) {
    double e = synth_held_envelope(&h, age, 100, 200, .4, 80);
    assert(isfinite(e) && e >= 0 && e <= 1);
    if (age >= 300) assert(e == .4);
  }
  for (int age = 0; age < 400; age += 25) {
    h = (SynthHeld){0}; h.voiceId=2;
    double before = synth_held_envelope(&h, age, 100, 200, .4, 80);
    synth_held_release(&h, before);
    assert(synth_held_envelope(&h, age, 100, 200, .4, 80) == before);
    for (int n=1; n<80; n++) {
      h.releasePlayed=n;
      double e=synth_held_envelope(&h, age+n, 100, 200, .4, 80);
      assert(e <= before); before=e;
    }
    assert(before == 0);
  }
  SynthLive live = {0};
  double initial[SYNTH_LIVE_COUNT] = {440,1,1000,.7071,0,0,.5,1,0};
  synth_live_init(&live,initial);
  double values[SYNTH_LIVE_COUNT] = {880,0,2000,1,5,10,.8,2,3};
  synth_live_update(&live,(1u<<SYNTH_LIVE_COUNT)-1,values,480);
  for(int i=0;i<479;i++) { synth_live_tick(&live); assert(live.value[LIVE_FREQUENCY] < 880); }
  synth_live_tick(&live); assert(live.value[LIVE_FREQUENCY]==880 && live.value[LIVE_VOLUME]==0);
  values[LIVE_FREQUENCY]=220; synth_live_update(&live,1u<<LIVE_FREQUENCY,values,480);
  synth_live_tick(&live); double current=live.value[0];
  values[0]=660; synth_live_update(&live,1u,values,480);
  assert(live.value[0]==current);
  for(int i=0;i<480;i++) synth_live_tick(&live);
  assert(live.value[0]==660 && live.value[1]==0);
  SynthLiveIdentity identity={0};
  synth_identity_set(&identity,42,synth_live_allowed(1,1));
  assert(synth_identity_mask(&identity,42)&(1<<LIVE_PULSE_WIDTH));
  assert(synth_identity_mask(&identity,41)==-1);
  assert(!(synth_live_allowed(1,1)&(1<<LIVE_FM_DEPTH)));
  puts("held envelope, smoothing and identity: PASS");
}
