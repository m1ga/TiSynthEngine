/* Full replacement must reserve storage without overwriting it; partial edits
 * must still inherit the last accepted bank. Both paths retain atomic failure. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../shared/synth_transport.h"
int main(void) {
 SynthTransport *t=calloc(1,sizeof(*t));assert(t);synth_transport_init(t,48000,1);
 memset(&t->slots[0].banks[0],0xa5,sizeof(SynthSequenceConfig));
 SynthSequenceConfig *c=synth_transport_begin_write_full(t,1,1);assert(c);
 assert((unsigned char)c->steps[511].notes[0]==0xa5);
 synth_sequence_defaults(c);c->stepCount=1;c->steps[0].noteCount=1;c->steps[0].notes[0]=60;
 assert(synth_sequence_prepare(c));assert(synth_transport_commit_prepared(t,c,1));synth_transport_begin_block(t);
 SynthSequenceEvent e;assert(synth_transport_tick(t,0,&e)&&e.notes[0]==60);
 c=synth_transport_begin_write(t,1,0);assert(c&&c->steps[0].notes[0]==60);c->steps[0].notes[0]=64;
 assert(synth_transport_commit(t,c,0));
 SynthSequenceConfig *failed=synth_transport_begin_write_full(t,1,0);assert(failed);
 synth_sequence_defaults(failed);failed->stepCount=0;assert(!synth_transport_commit(t,failed,0));
 synth_transport_begin_block(t);t->slots[0].beat=t->slots[0].nextBeat;
 assert(!synth_transport_tick(t,0,&e)); // finite one-step sequence is not restarted by an edit
 SynthSequenceEnd end;assert(synth_transport_pop_end(t,&end)&&end.sequenceId==1);
 assert(!synth_transport_begin_write_full(t,1,0));
 assert(!synth_sequence_commit(&t->slots[0],NULL,0));
 c=synth_transport_begin_write_full(t,2,1);assert(c);synth_transport_cancel_write(t,c);
 assert(synth_transport_find(t,2)<0); // no owner survives a cancelled restart
 free(t);puts("full bank replacement: no redundant copy, partial inheritance, rejected edit and reuse PASS");
}
