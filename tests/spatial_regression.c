#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#if __has_include("../shared/synth_spatial.h")
#include "../shared/synth_spatial.h"
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void impulse(float x, float y, int rate, float *l, float *r) {
  SynthSpatial s; synth_spatial_init(&s, x, y, rate);
  for (int i=0;i<640;i++) synth_spatial_process(&s, i==0 ? 1.0f:0.0f, &l[i], &r[i]);
}
static int consecutive_blocks_match_streaming(void) {
 const int rates[]={8000,16000,44100,48000,96000,192000};
 const int sizes[]={0,1,3,4,7,8,9,15,16,17,23,31};
 uint32_t seed=1;
 for(unsigned rate=0;rate<sizeof(rates)/sizeof(rates[0]);rate++) {
  SynthSpatial streaming,blocked;
  synth_spatial_init(&streaming,.6f,-.4f,rates[rate]);blocked=streaming;
  for(int block=0;block<256;block++) {
   if(block%13==0) {
    SynthSpatialPrepared target;
    synth_spatial_prepare(&target,(block%9-4)/4.0f,(block%7-3)/3.0f,rates[rate]);
    synth_spatial_set_target(&streaming,&target,1+block%17);
    synth_spatial_set_target(&blocked,&target,1+block%17);
   }
   int count=sizes[block%(sizeof(sizes)/sizeof(sizes[0]))];
   float input[31],left[31],right[31],expectedL[31],expectedR[31];
   for(int i=0;i<count;i++) {
    seed=seed*1664525u+1013904223u;
    input[i]=block%4==0?0.0f:(int32_t)seed/2147483648.0f;
    synth_spatial_process(&streaming,input[i],expectedL+i,expectedR+i);
   }
   synth_spatial_process_block(&blocked,input,left,right,count);
   REQUIRE(memcmp(left,expectedL,count*sizeof(float))==0);
   REQUIRE(memcmp(right,expectedR,count*sizeof(float))==0);
   REQUIRE(memcmp(&streaming,&blocked,sizeof(streaming))==0);
  }
 }
 return 0;
}
int main(void) {
 REQUIRE(consecutive_blocks_match_streaming()==0);
 float l[640],r[640],ll[640],rr[640],up[640],down[640];
 impulse(1,0,44100,l,r); impulse(-1,0,44100,ll,rr);
 double near=0,far=0;
 for(int i=0;i<640;i++){REQUIRE(fabs(l[i]-rr[i])<1e-6);REQUIRE(fabs(r[i]-ll[i])<1e-6);near+=r[i]*r[i];far+=l[i]*l[i];}
 REQUIRE(near>far);
 impulse(0,1,44100,up,r);impulse(0,-1,44100,down,r);
 double difference=0;for(int i=0;i<640;i++)difference+=fabs(up[i]-down[i]);REQUIRE(difference>0.1);
 for(int rate=16000;rate<=192000;rate*=2){impulse(.5f,.5f,rate,l,r);double energy=0;for(int i=0;i<640;i++){REQUIRE(isfinite(l[i])&&isfinite(r[i]));energy+=l[i]*l[i]+r[i]*r[i];}REQUIRE(energy>0.001&&energy<4);}
 impulse(0,0,48000,l,r);impulse(.00001f,0,48000,ll,rr);
 for(int i=0;i<640;i++) REQUIRE(fabs(l[i]-ll[i])<.0001&&fabs(r[i]-rr[i])<.0001);
 SynthSpatial s;synth_spatial_init(&s,.8f,-.2f,48000);float a,b;
 for(int i=0;i<100;i++)synth_spatial_process(&s,1,&a,&b);
 synth_spatial_init(&s,.8f,-.2f,48000);
 for(int i=0;i<640;i++){synth_spatial_process(&s,0,&a,&b);REQUIRE(a==0&&b==0);}
 puts("spatial_regression: mirror, elevation, continuity, sample rates and reset passed");return 0;
}
#else
int main(void) { fprintf(stderr,"FAIL: binaural processor is not implemented\n");return 1; }
#endif
