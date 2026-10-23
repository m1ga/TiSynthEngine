/* Independent pre-optimization four-accumulator reference. Compile/run on
 * Android/aarch64 with the same NDK/O2 flags as the shipped module. No audio
 * stream is opened. The reference body below must not call the current kernel.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../../shared/synth_spatial.h"
__attribute__((noinline)) static void reference_process(SynthSpatial *s,float input,float *left,float *right) {
  if(s->rampRemaining>0) {
   if(--s->rampRemaining==0) {
    memcpy(s->left,s->targetLeft,sizeof(s->left));memcpy(s->right,s->targetRight,sizeof(s->right));
   }else for(int i=0;i<s->taps;i++) {s->left[i]+=s->deltaLeft[i];s->right[i]+=s->deltaRight[i];}
  }
  int p=s->position;s->history[p]=input;s->history[p+s->taps]=input;
  const float *h=s->history+p;
  float l0=0,l1=0,l2=0,l3=0,r0=0,r1=0,r2=0,r3=0;
  for(int i=0;i<s->taps;i+=4) {
    l0+=h[i]*s->left[i];l1+=h[i+1]*s->left[i+1];l2+=h[i+2]*s->left[i+2];l3+=h[i+3]*s->left[i+3];
    r0+=h[i]*s->right[i];r1+=h[i+1]*s->right[i+1];r2+=h[i+2]*s->right[i+2];r3+=h[i+3]*s->right[i+3];
  }
  *left=(l0+l1)+(l2+l3);*right=(r0+r1)+(r2+r3);
  s->position=p?p-1:s->taps-1;
}
int main(){SynthSpatial*a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));float l,r,u,v;uint32_t seed=1;int bad=0,stateBad=0;uint64_t samples=0;
 const int rates[]={16000,44100,48000,96000,192000};
 for(int ri=0;ri<5;ri++){int rate=rates[ri];synth_spatial_init(a,.6,-.4,rate);*b=*a;int rateBad=0;
  for(int i=0;i<30000;i++){
   if(i%257==0){SynthSpatialPrepared prepared;float x=(float)((i/257)%9-4)/4,y=(float)((i/257)%7-3)/3;synth_spatial_prepare(&prepared,x,y,rate);synth_spatial_set_target(a,&prepared,rate/100);synth_spatial_set_target(b,&prepared,rate/100);}
   seed=seed*1664525+1013904223;float x=i%4==0?(int32_t)seed/2147483648.0f:i%4==1?sinf(i*.11f):i%4==2?0:i%37==0?1:0;
   reference_process(a,x,&l,&r);synth_spatial_process(b,x,&u,&v);if(memcmp(&l,&u,4)||memcmp(&r,&v,4)){bad++;rateBad++;}samples++;
   if(memcmp(a,b,sizeof(*a)))stateBad++;
  }
  printf("rate=%d differing_outputs=%d\n",rate,rateBad);
 }
 printf("samples=%llu different_samples=%d different_states=%d\n",(unsigned long long)samples,bad,stateBad);
 for(int mode=0;mode<2;mode++){synth_spatial_init(a,.6,-.4,48000);clock_t begin=clock();volatile float total=0;for(int i=0;i<500000;i++){if(mode)synth_spatial_process(a,.1,&l,&r);else reference_process(a,.1,&l,&r);total+=l;}printf("mode=%d cpu_seconds=%f sum=%f\n",mode,(clock()-begin)/(double)CLOCKS_PER_SEC,total);}
 return bad||stateBad?1:0;
}
