#define SYNTH_PHYSICAL_STATS 1
#include "synth_stats.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  SynthStats s;synth_stats_init(&s);synth_stats_enable(&s,true);assert(synth_stats_begin(&s));
  for(int i=0;i<94;i++)synth_stats_end(&s,1000,1000,.1);
  synth_stats_end(&s,1000,1000,.499);
  for(int i=0;i<5;i++)synth_stats_end(&s,1000,1000,1.1);
  uint64_t bins[2048],misses,underflow;synth_physical_read(&s,bins,&misses,&underflow);
  assert(synth_physical_word(&s.physicalFrames,(uint32_t)synth_stats_load(&s.physicalEpoch))==100000);
  assert(bins[100]==94&&bins[499]==1&&bins[1100]==5&&misses==5&&underflow==0);
  unsigned total=0,rank=95,index=0;for(;index<2048;index++){total+=bins[index];if(total>=rank)break;}
  assert(index==499);assert((index+1)/1000.0==.5);
  synth_physical_freeze(&s);synth_stats_end(&s,1000,1000,2.5);
  assert(synth_stats_load(&s.physicalFreeze)==2);synth_physical_read(&s,bins,&misses,&underflow);assert(bins[2047]==0&&misses==5);
  synth_stats_reset(&s);assert(synth_stats_begin(&s));synth_physical_read(&s,bins,&misses,&underflow);assert(misses==0&&bins[100]==0);
  synth_stats_end(&s,1000,1000,2.5);synth_stats_end(&s,1000,1000,-.1);
  synth_physical_read(&s,bins,&misses,&underflow);assert(bins[2047]==1&&bins[0]==1&&misses==1&&underflow==1);
  printf("Physical epoch histogram/count/p95/freeze/overflow PASS; SynthStats=%zu bytes\n",sizeof(s));
}
