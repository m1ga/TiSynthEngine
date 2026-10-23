#include "synth_effects.h"
#include "synth_transport.h"
#include <assert.h>
#include <stdio.h>
int main(void){int rates[]={44100,48000,96000,192000};
for(unsigned i=0;i<sizeof(rates)/sizeof(rates[0]);i++){
 int rate=rates[i],lookahead=(int)round(.01*rate);SynthEffects fx;
 assert(synth_effects_init(&fx,rate));
 size_t effectHeap=synth_effects_memory_bytes(&fx)-sizeof(SynthEffects);
 size_t limiterHeap=(size_t)(lookahead*2+(lookahead+1)*2)*8;
 printf("{\"sampleRate\":%d,\"effectsHeap\":%zu,\"limiterHeap\":%zu,\"transportHeap\":%zu,\"totalHeapOutsideModuleIvars\":%zu}\n",rate,effectHeap,limiterHeap,sizeof(SynthTransport),effectHeap+limiterHeap+sizeof(SynthTransport));
 synth_effects_destroy(&fx);
}}
