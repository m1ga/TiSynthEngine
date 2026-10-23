#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../shared/synth_transport.h"
static SynthTransport pool;
static void publish(unsigned id,int unit) {
 SynthSequenceConfig *c=synth_transport_begin_write(&pool,id,1);assert(c);
 c->stepCount=1;c->timeUnit=unit;c->stepDuration=unit==SYNTH_SEQUENCE_MS?100:.25;
 c->steps[0].noteCount=1;c->steps[0].notes[0]=69;
 assert(synth_transport_commit(&pool,c,1));
}
int main(void) {
 synth_transport_init(&pool,48000,4);
 for(unsigned id=1;id<=4;id++)publish(id,id==4?SYNTH_SEQUENCE_MS:SYNTH_SEQUENCE_BEATS);
 assert(!synth_transport_begin_write(&pool,5,1));
 synth_transport_begin_block(&pool);
 SynthSequenceState s;assert(synth_transport_state(&pool,1,&s));
 assert(!synth_transport_state(&pool,999,&s));
 SynthSequenceEvent e;for(int i=0;i<4;i++){assert(synth_transport_tick(&pool,i,&e));assert(e.sequenceId==(unsigned)i+1);}
 assert(synth_transport_request_stop(&pool,2));assert(!synth_transport_request_stop(&pool,999));
 assert(synth_transport_set_tempo(&pool,240));assert(!synth_transport_set_tempo(&pool,301));
 synth_transport_begin_block(&pool);assert(pool.bpm==240);
 assert(!synth_transport_state(&pool,2,&s));
 assert(!synth_transport_begin_write(&pool,5,1)); /* completion storage not reused */
 SynthSequenceEnd end;assert(synth_transport_pop_end(&pool,&end));assert(end.sequenceId==2&&end.reason==SYNTH_SEQUENCE_STOPPED);
 publish(5,SYNTH_SEQUENCE_BEATS);synth_transport_begin_block(&pool);
 assert(!synth_transport_request_stop(&pool,2));assert(synth_transport_state(&pool,5,&s));
 /* All other events finish once, even when no public calls occur during render. */
 for(int frame=0;frame<7000;frame++) {
  for(int i=0;i<4;i++)synth_transport_tick(&pool,i,&e);
  synth_transport_advance_sample(&pool);
 }
 int completed=0;while(synth_transport_pop_end(&pool,&end)){assert(end.reason==SYNTH_SEQUENCE_COMPLETED);completed++;}
 assert(completed==4);assert(!synth_transport_pop_end(&pool,&end));
 assert(pool.beat>.58&&pool.milliseconds>145);
 publish(6,SYNTH_SEQUENCE_BEATS);synth_transport_begin_block(&pool);
 int slot=synth_transport_find(&pool,6);assert(synth_transport_tick(&pool,slot,&e));
 SynthSequenceConfig *edit=synth_transport_begin_write(&pool,6,0);assert(edit);
 pool.slots[slot].beat=pool.slots[slot].nextBeat;
 assert(!synth_transport_tick(&pool,slot,&e));
 assert(!synth_transport_commit(&pool,edit,0));
 assert(synth_transport_pop_end(&pool,&end)&&end.sequenceId==6);
 assert(sizeof(pool)<12u*1024u*1024u);
 puts("transport pool: capacity, four independent sequences, tempo, stale IDs, durable completions and reuse PASS");
}
