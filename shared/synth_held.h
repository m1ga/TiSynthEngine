/* Sustained envelope, 10 ms parameter ramps and lock-free identity snapshots.
 * Plain C shared by both callbacks. No allocation, locks or external calls. */
#ifndef SYNTH_HELD_H
#define SYNTH_HELD_H
#include <stdint.h>
#include <string.h>
#include "synth_core.h"

enum { LIVE_FREQUENCY, LIVE_VOLUME, LIVE_FILTER_HZ, LIVE_FILTER_Q,
       LIVE_LFO_FREQ, LIVE_LFO_DEPTH, LIVE_PULSE_WIDTH, LIVE_FM_RATIO,
       LIVE_FM_DEPTH, LIVE_NOISE_RATE, LIVE_LOWPASS_HZ, LIVE_PAN, SYNTH_LIVE_COUNT };
#define SYNTH_CMD_HELD_ON 4
#define SYNTH_CMD_HELD_OFF 5
#define SYNTH_CMD_HELD_UPDATE 6

typedef struct {
  int32_t voiceId;
  int releasing;
  int releasePlayed;
  double releaseLevel;
  uint64_t order;
} SynthHeld;

/* A zero ADSR edge is a hard gate. Keep sustained key edges click-safe. */
static inline int synth_held_edge_samples(double sampleRate) {
  int frames=(int)(sampleRate*.003+.5);
  return frames>1 ? frames : 1;
}

static inline void synth_held_release(SynthHeld *h, double level) {
  if (h->releasing) return; /* repeated noteOff never lengthens the release */
  h->releasing = 1; h->releasePlayed = 0; h->releaseLevel = level;
}
static inline double synth_held_envelope(const SynthHeld *h, int age, int attack,
                                        int decay, double sustain, int release) {
  if (h->releasing) {
    if (release <= 1 || h->releasePlayed >= release) return 0;
    return h->releaseLevel * synth_smoothstep(1.0-(double)h->releasePlayed/(release-1));
  }
  if (age < attack) return synth_smoothstep((double)age/(attack>2 ? attack-1 : 1));
  if (age < attack+decay) return 1+(sustain-1)*synth_smoothstep((double)(age-attack)/(decay>2 ? decay-1 : 1));
  return sustain;
}

typedef struct {
  double value[SYNTH_LIVE_COUNT], target[SYNTH_LIVE_COUNT], step[SYNTH_LIVE_COUNT];
  int remaining[SYNTH_LIVE_COUNT];
  uint32_t activeMask;
} SynthLive;
static inline void synth_live_init(SynthLive *s, const double *values) {
  memset(s,0,sizeof(*s));
  memcpy(s->value,values,sizeof(s->value)); memcpy(s->target,values,sizeof(s->target));
}
static inline void synth_live_update(SynthLive *s, uint32_t mask, const double *values, int frames) {
  if (frames < 1) frames=1;
  for(int i=0;i<SYNTH_LIVE_COUNT;i++) if (mask & (1u<<i)) {
    if(s->target[i]==values[i])continue;
    s->target[i]=values[i]; s->remaining[i]=frames;s->activeMask|=1u<<i;
    s->step[i]=(values[i]-s->value[i])/frames;
  }
}
static inline void synth_live_tick(SynthLive *s) {
  if(!s->activeMask)return;
  for(int i=0;i<SYNTH_LIVE_COUNT;i++) if(s->activeMask&(1u<<i)) {
    if(--s->remaining[i]==0) {s->value[i]=s->target[i];s->activeMask&=~(1u<<i);}
    else s->value[i]+=s->step[i];
  }
}
static inline uint32_t synth_live_allowed(int waveType, int filterType) {
  uint32_t mask=(1u<<LIVE_NOISE_RATE)|(1u<<LIVE_LOWPASS_HZ)|(1u<<LIVE_PAN)|(1u<<LIVE_FREQUENCY)|(1u<<LIVE_VOLUME)|(1u<<LIVE_LFO_FREQ)|(1u<<LIVE_LFO_DEPTH);
  if(filterType) mask|=(1u<<LIVE_FILTER_HZ)|(1u<<LIVE_FILTER_Q);
  if(waveType==1) mask|=1u<<LIVE_PULSE_WIDTH;
  if(waveType==0) mask|=(1u<<LIVE_FM_RATIO)|(1u<<LIVE_FM_DEPTH);
  return mask;
}

/* One writer (callback), arbitrary readers. Only 32-bit lock-free atomics;
 * readers may retry, writer always finishes without waiting. */
#ifdef __cplusplus
static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0), "Voice identity requires lock-free 32-bit atomics");
#else
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0), "Voice identity requires lock-free 32-bit atomics");
#endif
typedef struct { uint32_t sequence, voiceId, mask; } SynthLiveIdentity;
static inline void synth_identity_set(SynthLiveIdentity *s, int32_t id, uint32_t mask) {
  __atomic_fetch_add(&s->sequence,1u,__ATOMIC_SEQ_CST);
  __atomic_store_n(&s->voiceId,(uint32_t)id,__ATOMIC_SEQ_CST);
  __atomic_store_n(&s->mask,mask,__ATOMIC_SEQ_CST);
  __atomic_fetch_add(&s->sequence,1u,__ATOMIC_SEQ_CST);
}
static inline int synth_identity_mask(const SynthLiveIdentity *s, int32_t id) {
  for(;;) {
    uint32_t before=__atomic_load_n(&s->sequence,__ATOMIC_SEQ_CST);
    if(before&1u) continue;
    uint32_t found=__atomic_load_n(&s->voiceId,__ATOMIC_SEQ_CST);
    uint32_t mask=__atomic_load_n(&s->mask,__ATOMIC_SEQ_CST);
    if(before==__atomic_load_n(&s->sequence,__ATOMIC_SEQ_CST)) return found==(uint32_t)id ? (int)mask : -1;
  }
}
#endif
