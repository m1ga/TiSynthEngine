#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../shared/synth_spatial.h"
#include "../shared/synth_held.h"
static SynthSpatial s;
int main(void) {
 synth_spatial_init(&s,-1,0,48000);
 float l,r;for(int i=0;i<100;i++)synth_spatial_process(&s,i==99?1:0,&l,&r);
 int position=s.position;float history[SYNTH_SPATIAL_MAX_TAPS*2];memcpy(history,s.history,sizeof(history));
 SynthSpatialPrepared target;synth_spatial_prepare(&target,1,1,48000);
 float start=s.left[10];synth_spatial_set_target(&s,&target,480);
 assert(position==s.position&&!memcmp(history,s.history,sizeof(history)));
 for(int i=0;i<240;i++)synth_spatial_process(&s,0,&l,&r);
 assert(fabs(s.left[10]-(start+target.left[10])*.5)<1e-5);
 for(int i=0;i<240;i++)synth_spatial_process(&s,0,&l,&r);
 assert(!memcmp(s.left,target.left,sizeof(s.left))&&s.rampRemaining==0);
 SynthLive live;double values[SYNTH_LIVE_COUNT]={0};synth_live_init(&live,values);
 values[LIVE_NOISE_RATE]=1;values[LIVE_LOWPASS_HZ]=1;values[LIVE_PAN]=-1;
 synth_live_update(&live,(1u<<LIVE_NOISE_RATE)|(1u<<LIVE_LOWPASS_HZ)|(1u<<LIVE_PAN),values,480);
 for(int i=0;i<480;i++)synth_live_tick(&live);
 assert(live.value[LIVE_NOISE_RATE]==1&&live.value[LIVE_LOWPASS_HZ]==1&&live.value[LIVE_PAN]==-1);
 puts("spatial/control update: prepared coefficients, retained history and exact 10ms ramps PASS");
}
