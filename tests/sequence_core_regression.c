#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../shared/synth_sequence.h"
static SynthSequenceTransport transport;
static SynthSequenceConfig *start(double rate,int count,int mode) {
 synth_sequence_init(&transport,rate);
 SynthSequenceConfig *c=synth_sequence_begin_write(&transport);assert(c);
 c->stepCount=count;c->mode=mode;c->loop=1;
 for(int i=0;i<count;i++){c->steps[i].noteCount=1;c->steps[i].notes[0]=(uint8_t)(69+i);c->steps[i].gate=.5;}
 assert(synth_sequence_commit(&transport,c,1));synth_sequence_begin_block(&transport);return c;
}
static void timing(void) {
 const int rates[]={16000,44100,48000,96000,192000};
 for(int r=0;r<5;r++) {
  int rate=rates[r],events=0;start(rate,1,0);SynthSequenceEvent e;
  /* Ten uninterrupted minutes at an intentionally fractional step period. */
  transport.bpm=137;
  for(int64_t frame=0;frame<(int64_t)rate*600;frame++) {
   if(synth_sequence_tick(&transport,&e)) {
    double expected=events*(60.0*rate/(137*4));
    assert(fabs(frame-expected)<=1.00001);events++;
   }
   synth_sequence_advance_sample(&transport);
  }
  assert(events==5480);
 }
}
static void ordering(void) {
 SynthSequenceConfig *c=start(48000,4,SYNTH_SEQUENCE_UPDOWN);
 SynthSequenceEvent e;
 /* Restart with chord, rest and unsorted original step indices. */
 c=synth_sequence_begin_write(&transport);assert(c);
 c->steps[0].notes[0]=72;c->steps[1].rest=1;c->steps[2].notes[0]=60;
 c->steps[3].notes[0]=67;c->steps[3].notes[1]=67;c->steps[3].noteCount=2;
 assert(synth_sequence_commit(&transport,c,1));synth_sequence_begin_block(&transport);
 const int expected[]={2,3,0,3,2};
 for(int i=0;i<5;i++) {
  transport.beat=transport.nextBeat;assert(synth_sequence_tick(&transport,&e));assert(e.stepIndex==expected[i]);
  if(e.stepIndex==3)assert(e.noteCount==2&&e.notes[0]==e.notes[1]);
 }
 assert(fabs(synth_sequence_frequency(c,69)-440)<1e-9);
}
static void publication(void) {
 start(48000,4,0);SynthSequenceEvent e;assert(synth_sequence_tick(&transport,&e));
 SynthSequenceConfig *c=synth_sequence_begin_write(&transport);assert(c);c->bpm=240;c->stepCount=2;
 assert(synth_sequence_commit(&transport,c,0));
 SynthSequenceConfig *d=synth_sequence_begin_write(&transport);assert(d);assert(d->bpm==240);d->bpm=250;
 assert(!synth_sequence_commit(&transport,d,0)); /* mailbox cannot overwrite */
 synth_sequence_begin_block(&transport);assert(transport.bpm==240);
 assert(transport.banks[transport.active].stepCount==4); /* structural edit waits */
 c=synth_sequence_begin_write(&transport);assert(c);d=synth_sequence_begin_write(&transport);assert(!d);
 synth_sequence_cancel_write(&transport,c);
 transport.beat=transport.nextBeat;assert(synth_sequence_tick(&transport,&e));
 assert(transport.banks[transport.active].stepCount==2&&e.stepIndex==1);
 c=synth_sequence_begin_write(&transport);assert(c);c->steps[0].notes[0]=11;
 assert(!synth_sequence_commit(&transport,c,0));
 c=synth_sequence_begin_write(&transport);assert(c);c->bpm=NAN;assert(!synth_sequence_commit(&transport,c,0));
 synth_sequence_publish_state(&transport);SynthSequenceState s=synth_sequence_state(&transport);
 assert(s.playing&&s.stepIndex==1&&s.bpm==240&&s.revision==2);
 synth_sequence_request_stop(&transport);
 c=synth_sequence_begin_write(&transport);assert(c);assert(synth_sequence_commit(&transport,c,1));
 synth_sequence_begin_block(&transport);assert(transport.playing); /* newer play supersedes stop */
 uint64_t generation=transport.generation;synth_sequence_request_stop(&transport);synth_sequence_begin_block(&transport);
 assert(!transport.playing&&transport.generation==generation+1);
}
static void swing_and_end(void) {
 start(48000,4,0);SynthSequenceEvent e;
 SynthSequenceConfig *c=synth_sequence_begin_write(&transport);c->swing=.5;c->loop=0;
 assert(synth_sequence_commit(&transport,c,1));synth_sequence_begin_block(&transport);
 assert(synth_sequence_tick(&transport,&e));assert(fabs(e.gateEndBeat-.1875)<1e-12);
 double end=transport.nextBeat;
 c=synth_sequence_begin_write(&transport);assert(c);c->swing=.75;c->stepCount=2;
 assert(synth_sequence_commit(&transport,c,0));synth_sequence_begin_block(&transport);
 transport.beat=end;assert(synth_sequence_tick(&transport,&e));assert(fabs(transport.nextBeat-.5)<1e-12);
 transport.beat=transport.nextBeat;assert(!synth_sequence_tick(&transport,&e));assert(!transport.playing);
}
static void legacy_tuning_and_negative_swing(void) {
 start(48000,2,0);SynthSequenceEvent e;
 SynthSequenceConfig *c=synth_sequence_begin_write(&transport);assert(c);
 c->swing=-.75;c->tuning=220;
 assert(synth_sequence_commit(&transport,c,1));synth_sequence_begin_block(&transport);
 int events=0;
 for(int frame=0;frame<=12000;frame++) {
  if(synth_sequence_tick(&transport,&e)) {
   const int expected[]={0,1500,12000};assert(events<3);assert(abs(frame-expected[events])<=1);events++;
  }
  synth_sequence_advance_sample(&transport);
 }
 assert(events==3);
 c=synth_sequence_begin_write(&transport);assert(c);c->tuning=880;
 assert(synth_sequence_commit(&transport,c,0));synth_sequence_begin_block(&transport);
 assert(fabs(synth_sequence_frequency(c,69)-880)<1e-12);
 assert(synth_sequence_frequency(c,119)>16000*.45); /* bridge preflight rejects this pitch */
 c=synth_sequence_begin_write(&transport);assert(c);c->tuning=219;assert(!synth_sequence_commit(&transport,c,0));
 c=synth_sequence_begin_write(&transport);assert(c);c->tuning=881;assert(!synth_sequence_commit(&transport,c,0));
 c=synth_sequence_begin_write(&transport);assert(c);c->swing=-.751;assert(!synth_sequence_commit(&transport,c,0));
}
int main(void) {
 legacy_tuning_and_negative_swing();timing();ordering();publication();swing_and_end();puts("sequence core regression passed: ten-minute sample clock at five rates, chords, arpeggios, edits, publication, swing and stop");
}
