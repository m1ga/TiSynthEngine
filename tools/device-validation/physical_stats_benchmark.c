#include "synth_stats.h"
#include <stdio.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
SynthStats s;
int main(void){synth_stats_init(&s);synth_stats_enable(&s,true);synth_stats_begin(&s);
for(int i=0;i<10000;i++)synth_stats_end(&s,1024,44100,.003);
double start=now();for(int i=0;i<2000000;i++)synth_stats_end(&s,1024,44100,.003+(i%7)*1e-6);
printf("%.3f\n",(now()-start)*1e9/2000000);}
