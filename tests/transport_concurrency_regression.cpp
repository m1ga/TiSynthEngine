// The control side is serialized by each module; only audio runs concurrently.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include "../shared/synth_transport.h"
static SynthTransport t;
int main() {
 synth_transport_init(&t,48000,4);std::atomic<bool> done{false};
 std::thread audio([&] {
  while(!done.load(std::memory_order_acquire)) {
   synth_transport_begin_block(&t);
   for(int f=0;f<64;f++) {
    SynthSequenceEvent e;
    for(int i=0;i<4;i++)synth_transport_tick(&t,i,&e);
    synth_transport_advance_sample(&t);
   }
  }
 });
 for(uint32_t id=1;id<=1000;id++) {
  auto *c=synth_transport_begin_write(&t,id,1);assert(c);
  c->timeUnit=SYNTH_SEQUENCE_MS;c->stepDuration=1;c->stepCount=1;c->steps[0].noteCount=1;c->steps[0].notes[0]=60;
  assert(synth_transport_commit(&t,c,1));
  if(id&1)synth_transport_request_stop(&t,id);
  SynthSequenceState s;synth_transport_state(&t,id,&s);
  SynthSequenceEnd e;
  while(!synth_transport_pop_end(&t,&e))std::this_thread::yield();
  assert(e.sequenceId==id&&(e.reason==SYNTH_SEQUENCE_COMPLETED||e.reason==SYNTH_SEQUENCE_STOPPED));
  assert(!synth_transport_request_stop(&t,id));assert(!synth_transport_state(&t,id,&s));
 }
 done.store(true,std::memory_order_release);audio.join();
 puts("transport concurrency: 1000 publish/stop/complete/snapshot/retire/reuse cycles PASS");
}
