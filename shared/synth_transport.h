/* A bounded collection of sample-clock sequences. One serialized control
 * producer, one audio consumer. End records occupy retired slots until drained
 * on the control thread; publication never overwrites an undelivered record. */
#ifndef SYNTH_TRANSPORT_H
#define SYNTH_TRANSPORT_H
#include "synth_sequence.h"
#define SYNTH_TRANSPORT_MAX_SEQUENCES 4
enum { SYNTH_SEQUENCE_COMPLETED=1, SYNTH_SEQUENCE_STOPPED=2 };
typedef struct { uint32_t sequenceId; int reason; } SynthSequenceEnd;
typedef struct {
 SynthSequenceTransport slots[SYNTH_TRANSPORT_MAX_SEQUENCES];
 uint32_t ids[SYNTH_TRANSPORT_MAX_SEQUENCES],status[SYNTH_TRANSPORT_MAX_SEQUENCES];
 uint32_t reasons[SYNTH_TRANSPORT_MAX_SEQUENCES],latestStoppedId[SYNTH_TRANSPORT_MAX_SEQUENCES];
 int capacity,clockRunning;
 uint32_t activeMask;
 double sampleRate,bpm,beat,milliseconds,beatCompensation,msCompensation;
 uint64_t pendingTempo __attribute__((aligned(8)));
} SynthTransport;
static inline void synth_transport_init(SynthTransport *t,double rate,int capacity) {
 memset(t,0,sizeof(*t));t->capacity=capacity<1?1:capacity>4?4:capacity;
 t->sampleRate=rate;t->bpm=120;
 for(int i=0;i<t->capacity;i++)synth_sequence_init(&t->slots[i],rate);
}
static inline int synth_transport_find(const SynthTransport *t,uint32_t id) {
 if(!id)return -1;
 for(int i=0;i<t->capacity;i++)if(__atomic_load_n(&t->ids[i],__ATOMIC_ACQUIRE)==id)return i;
 return -1;
}
static inline int synth_transport_bank_slot(SynthTransport *t,const SynthSequenceConfig *c) {
 for(int i=0;i<t->capacity;i++)if(synth_sequence_bank_index(&t->slots[i],c)>=0)return i;
 return -1;
}
static inline SynthSequenceConfig *synth_transport_begin_write(SynthTransport *t,uint32_t id,int restart) {
 if(!id)return 0;
 int index=synth_transport_find(t,id);
 if(restart) {
  if(index>=0)return 0;
  for(int i=0;i<t->capacity;i++) {
   uint32_t empty=0;
   if(__atomic_compare_exchange_n(&t->status[i],&empty,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) {
    index=i;__atomic_store_n(&t->ids[i],id,__ATOMIC_RELEASE);break;
   }
  }
 }else if(index<0||__atomic_load_n(&t->status[index],__ATOMIC_ACQUIRE)!=2)return 0;
 if(index<0)return 0;
 SynthSequenceConfig *c=synth_sequence_begin_write(&t->slots[index]);
 if(!c&&restart) {__atomic_store_n(&t->ids[index],0,__ATOMIC_RELEASE);__atomic_store_n(&t->status[index],0,__ATOMIC_RELEASE);}
 return c;
}
static inline void synth_transport_cancel_write(SynthTransport *t,SynthSequenceConfig *c) {
 int i=synth_transport_bank_slot(t,c);if(i<0)return;
 synth_sequence_cancel_write(&t->slots[i],c);
 if(__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)==1) {
  __atomic_store_n(&t->ids[i],0,__ATOMIC_RELEASE);__atomic_store_n(&t->status[i],0,__ATOMIC_RELEASE);
 }
}
static inline int synth_transport_commit(SynthTransport *t,SynthSequenceConfig *c,int restart) {
 int i=synth_transport_bank_slot(t,c);if(i<0)return 0;
 uint32_t status=__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE);
 if((restart&&status!=1)||(!restart&&status!=2)) {synth_sequence_cancel_write(&t->slots[i],c);return 0;}
 /* Tempo is published independently; a structural bank cannot change it. */
 c->bpm=120;
 if(!synth_sequence_commit(&t->slots[i],c,restart)) {
  if(__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)==1) {
   __atomic_store_n(&t->ids[i],0,__ATOMIC_RELEASE);__atomic_store_n(&t->status[i],0,__ATOMIC_RELEASE);
  }
  return 0;
 }
 if(restart)__atomic_store_n(&t->status[i],2,__ATOMIC_RELEASE);
 return 1;
}
static inline int synth_transport_request_stop(SynthTransport *t,uint32_t id) {
 int i=synth_transport_find(t,id);
 if(i<0||__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)!=2)return 0;
 synth_sequence_request_stop(&t->slots[i]);return 1;
}
static inline void synth_transport_stop_all(SynthTransport *t) {
 for(int i=0;i<t->capacity;i++)if(__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)==2)synth_sequence_request_stop(&t->slots[i]);
}
static inline int synth_transport_state(SynthTransport *t,uint32_t id,SynthSequenceState *out) {
 int i=synth_transport_find(t,id);
 if(i<0||__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)!=2)return 0;
 *out=synth_sequence_state(&t->slots[i]);
 return __atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)==2&&__atomic_load_n(&t->ids[i],__ATOMIC_ACQUIRE)==id;
}
static inline int synth_transport_set_tempo(SynthTransport *t,double bpm) {
 if(!isfinite(bpm)||bpm<20||bpm>300)return 0;
 uint64_t bits;memcpy(&bits,&bpm,8);__atomic_store_n(&t->pendingTempo,bits,__ATOMIC_RELEASE);return 1;
}
/* Audio-thread only. */
static inline void synth_transport_retire(SynthTransport *t,int i,int reason) {
 synth_sequence_publish_state(&t->slots[i]);
 t->activeMask&=~(1u<<i);
 t->reasons[i]=(uint32_t)reason;
 if(reason==SYNTH_SEQUENCE_STOPPED)__atomic_store_n(&t->latestStoppedId[i],__atomic_load_n(&t->ids[i],__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
 __atomic_store_n(&t->status[i],3,__ATOMIC_RELEASE);
}
static inline void synth_transport_begin_block(SynthTransport *t) {
 uint64_t bits=__atomic_exchange_n(&t->pendingTempo,0,__ATOMIC_ACQ_REL);if(bits)memcpy(&t->bpm,&bits,8);
 t->activeMask=0;
 for(int i=0;i<t->capacity;i++)if(__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)==2) {
  t->activeMask|=1u<<i;t->clockRunning=1;
  SynthSequenceTransport *s=&t->slots[i];
  synth_sequence_begin_block(s);s->bpm=t->bpm;synth_sequence_publish_state(s);
  if(!s->playing)synth_transport_retire(t,i,SYNTH_SEQUENCE_STOPPED);
 }
}
static inline int synth_transport_tick(SynthTransport *t,int i,SynthSequenceEvent *e) {
 if(i<0||i>=t->capacity||!(t->activeMask&(1u<<i)))return 0;
 int fired=synth_sequence_tick(&t->slots[i],e);
 if(fired)e->sequenceId=__atomic_load_n(&t->ids[i],__ATOMIC_ACQUIRE);
 if(!t->slots[i].playing)synth_transport_retire(t,i,SYNTH_SEQUENCE_COMPLETED);
 return fired;
}
static inline void synth_transport_advance_sample(SynthTransport *t) {
 if(!t->clockRunning)return;
 double increment=t->bpm/(60*t->sampleRate)-t->beatCompensation;
 double next=t->beat+increment;t->beatCompensation=(next-t->beat)-increment;t->beat=next;
 increment=1000/t->sampleRate-t->msCompensation;next=t->milliseconds+increment;
 t->msCompensation=(next-t->milliseconds)-increment;t->milliseconds=next;
 for(int i=0;i<t->capacity;i++)if(t->activeMask&(1u<<i))synth_sequence_advance_sample(&t->slots[i]);
}
/* Control-thread only. A slot is reset before release to the producer. */
static inline int synth_transport_pop_end(SynthTransport *t,SynthSequenceEnd *e) {
 for(int i=0;i<t->capacity;i++)if(__atomic_load_n(&t->status[i],__ATOMIC_ACQUIRE)==3) {
  e->sequenceId=__atomic_load_n(&t->ids[i],__ATOMIC_ACQUIRE);e->reason=(int)t->reasons[i];
  synth_sequence_init(&t->slots[i],t->sampleRate);
  __atomic_store_n(&t->ids[i],0,__ATOMIC_RELEASE);__atomic_store_n(&t->status[i],0,__ATOMIC_RELEASE);return 1;
 }
 return 0;
}
#endif
