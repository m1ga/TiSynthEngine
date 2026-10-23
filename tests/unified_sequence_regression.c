#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../shared/synth_sequence.h"
static SynthSequenceTransport t;
static SynthSequenceConfig *begin(int unit) {
 synth_sequence_init(&t,48000);
 SynthSequenceConfig *c=synth_sequence_begin_write(&t);assert(c);
 c->timeUnit=unit;c->stepDuration=unit==SYNTH_SEQUENCE_MS?100:.25;c->stepCount=2;
 for(int i=0;i<2;i++){c->steps[i].noteCount=1;c->steps[i].notes[0]=69;}
 return c;
}
int main(void) {
 SynthSequenceEvent e;
 SynthSequenceConfig *c=begin(SYNTH_SEQUENCE_BEATS);
 assert(c->loop==0);c->steps[0].duration=.5;c->steps[1].duration=.25;c->swing=.5;
 c->steps[0].length=1;c->steps[0].frequencies[0]=333;
 assert(synth_sequence_commit(&t,c,1));synth_sequence_begin_block(&t);
 assert(synth_sequence_tick(&t,&e));assert(e.frequencies[0]==333);assert(e.length==1);
 assert(fabs(t.nextBeat-.625)<1e-12);assert(e.gateEndBeat==1);
 t.beat=t.nextBeat;assert(synth_sequence_tick(&t,&e));assert(t.nextBeat==.75);
 t.beat=t.nextBeat;assert(!synth_sequence_tick(&t,&e)&&!t.playing);
 c=begin(SYNTH_SEQUENCE_MS);c->loop=1;c->steps[0].duration=17;c->steps[1].duration=43;
 assert(synth_sequence_commit(&t,c,1));synth_sequence_begin_block(&t);
 int events=0;for(int f=0;f<48000;f++) {
  if(f==19200)t.bpm=240;
  if(synth_sequence_tick(&t,&e)) {
   double expected=(events/2)*2880+(events%2)*816;
   assert(fabs(f-expected)<=1.0001);events++;
  }
  synth_sequence_advance_sample(&t);
 }
 assert(events==34);
 c=begin(SYNTH_SEQUENCE_BEATS);c->stepCount=1;
 assert(synth_sequence_commit(&t,c,1));synth_sequence_begin_block(&t);
 assert(synth_sequence_tick(&t,&e));
 c=synth_sequence_begin_write(&t);assert(c);c->steps[0].voice.volume=.5;
 assert(synth_sequence_commit(&t,c,0));synth_sequence_begin_block(&t);
 t.beat=t.nextBeat;assert(!synth_sequence_tick(&t,&e)&&!t.playing);
 c=begin(SYNTH_SEQUENCE_BEATS);c->stepDuration=.000001;assert(!synth_sequence_commit(&t,c,1));
 c=begin(SYNTH_SEQUENCE_MS);c->steps[0].duration=.000001;assert(!synth_sequence_commit(&t,c,1));
 puts("unified sequence: default completion, unequal swing, explicit length, frequencies and ms tempo isolation PASS");
}
