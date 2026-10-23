#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../shared/synth_transport.h"
static SynthTransport pool;
int main(void) {
 int rates[]={16000,44100,48000,96000,192000};
 for(int r=0;r<5;r++) {
  int rate=rates[r];synth_transport_init(&pool,rate,4);
  for(unsigned id=1;id<=4;id++) {
   SynthSequenceConfig *c=synth_transport_begin_write(&pool,id,1);assert(c);
   c->timeUnit=id==4?SYNTH_SEQUENCE_MS:SYNTH_SEQUENCE_BEATS;c->stepDuration=id==4?17:.125*id;
   c->stepCount=2;c->loop=1;c->swing=id==2?.5:0;
   for(int i=0;i<2;i++){c->steps[i].noteCount=1;c->steps[i].notes[0]=(uint8_t)(60+id);}
   assert(synth_transport_commit(&pool,c,1));
  }
  assert(synth_transport_set_tempo(&pool,137));
  long double musicalOffset=0,deadline[4]={0,0,0,0};
  int64_t segmentStart=0;
  unsigned events[4]={0,0,0,0};int bpm=137;
  for(int64_t frame=0;frame<(int64_t)rate*600;frame++) {
   if(frame%256==0) {
    if(frame>=(int64_t)rate*120&&bpm==137){musicalOffset+=(long double)(frame-segmentStart)*bpm/(60*rate);segmentStart=frame;bpm=243;assert(synth_transport_set_tempo(&pool,bpm));}
    if(frame>=(int64_t)rate*360&&bpm==243){musicalOffset+=(long double)(frame-segmentStart)*bpm/(60*rate);segmentStart=frame;bpm=71;assert(synth_transport_set_tempo(&pool,bpm));}
    synth_transport_begin_block(&pool);
   }
   for(int slot=0;slot<4;slot++) {
    SynthSequenceEvent e;
    if(synth_transport_tick(&pool,slot,&e)) {
     long double now=slot==3?(long double)frame*1000/rate:musicalOffset+(long double)(frame-segmentStart)*bpm/(60*rate);
     long double increment=slot==3?1000.L/rate:(long double)bpm/(60*rate);
     if(!(now-deadline[slot]>=-1e-8L&&now-deadline[slot]<=increment+1e-8L)){fprintf(stderr,"rate%d frame%lld slot%d bpm%d error%Lg sample%Lg core%g expected%Lg\n",rate,(long long)frame,slot,bpm,now-deadline[slot],increment,pool.slots[slot].beat,deadline[slot]);abort();}
     long double interval=slot==3?17.L:.125L*(slot+1);
     if(slot==1)interval*=events[slot]%2?.5L:1.5L;
     deadline[slot]+=interval;events[slot]++;
    }
   }
   synth_transport_advance_sample(&pool);

  }
  for(int i=0;i<4;i++)assert(events[i]>1000);
 }
 puts("four-sequence clock: ten minutes at five rates, tempo edits, swing and independent ms intervals <=1 sample PASS");
}
