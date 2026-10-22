#ifndef SYNTH_STATS_H
#define SYNTH_STATS_H
/* One render owner, any number of readers. Only readers retry the seqlock;
 * the callback publishes once without allocation, locks or waits. Atomics use
 * integer bit patterns so C and C++ share the exact representation. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
/* 32-bit Android ABIs otherwise align uint64_t to four bytes and may call a
 * locking libatomic fallback. Make every published/control word eight-aligned. */
typedef uint64_t SynthStatsAtomic __attribute__((aligned(8)));
#if defined(__cplusplus)
static_assert(__atomic_always_lock_free(8, 0), "Statistics require lock-free 64-bit atomics");
#else
_Static_assert(__atomic_always_lock_free(8, 0), "Statistics require lock-free 64-bit atomics");
#endif
enum { ST_ACTIVE, ST_PEAK_ACTIVE, ST_TAILS, ST_SPATIAL, ST_STEALS,
  ST_CALLBACK, ST_LOAD, ST_PEAK_LOAD, ST_PEAK_L, ST_PEAK_R, ST_RMS_L,
  ST_RMS_R, ST_REDUCTION, ST_PRE_LIMITER_PEAK, ST_MAX_REDUCTION, SYNTH_STATS_COUNT };
typedef struct {
  SynthStatsAtomic sequence, config, reset, publishedConfig, publishedReset;
  SynthStatsAtomic published[SYNTH_STATS_COUNT];
  /* Render-owned from here down. */
  uint64_t seenConfig, seenReset;
  int active, peakActive, tails, spatial;
  uint64_t steals;
  bool enabled;
  int windowFrames;
  double peakL, peakR, sumL, sumR, minGain, peakLoad, maxPreLimiter, minEverGain;
  double values[SYNTH_STATS_COUNT];
} SynthStats;
static inline uint64_t synth_stats_load(const SynthStatsAtomic *p) { return __atomic_load_n(p,__ATOMIC_ACQUIRE); }
static inline void synth_stats_store(SynthStatsAtomic *p,uint64_t v) { __atomic_store_n(p,v,__ATOMIC_RELEASE); }
static inline uint64_t synth_stats_bits(double d) { uint64_t v; memcpy(&v,&d,8); return v; }
static inline double synth_stats_double(uint64_t v) { double d; memcpy(&d,&v,8); return d; }
static inline double synth_stats_db(double v) { return 20.0*log10(fmax(1e-6,v)); }
static inline void synth_stats_window(SynthStats *s) {
  s->windowFrames=0; s->peakL=s->peakR=s->sumL=s->sumR=0; s->minGain=1;
}
static inline void synth_stats_init(SynthStats *s) {
  memset(s,0,sizeof(*s));
  for(int i=ST_LOAD;i<SYNTH_STATS_COUNT;i++) { s->values[i]=NAN; s->published[i]=synth_stats_bits(NAN); }
  synth_stats_window(s);s->minEverGain=1;
}
static inline void synth_stats_enable(SynthStats *s,bool enabled) {
  uint64_t c=synth_stats_load(&s->config);
  synth_stats_store(&s->config,((c+2)&~(uint64_t)1)|(enabled?1:0));
}
static inline void synth_stats_reset(SynthStats *s) { __atomic_fetch_add(&s->reset,1,__ATOMIC_RELEASE); }
static inline bool synth_stats_begin(SynthStats *s) {
  uint64_t c=synth_stats_load(&s->config), r=synth_stats_load(&s->reset);
  if(c!=s->seenConfig || r!=s->seenReset) {
    if(r!=s->seenReset) { s->steals=0; s->peakActive=s->active; s->maxPreLimiter=0; s->minEverGain=1; }
    s->seenConfig=c; s->seenReset=r; s->enabled=(c&1)!=0;
    s->peakLoad=0; synth_stats_window(s);
    for(int i=ST_LOAD;i<SYNTH_STATS_COUNT;i++) s->values[i]=NAN;
  }
  return s->enabled;
}
static inline void synth_stats_voice_start(SynthStats *s,bool occupied,bool oldSpatial,bool spatial) {
  if(occupied) { s->steals++; s->spatial-=oldSpatial; } else s->active++;
  s->spatial+=spatial;
  if(s->active>s->peakActive) s->peakActive=s->active;
}
static inline void synth_stats_voice_end(SynthStats *s,bool spatial) { s->active--; s->spatial-=spatial; }
static inline void synth_stats_tail_start(SynthStats *s,bool occupied) { if(!occupied) s->tails++; }
static inline void synth_stats_tail_end(SynthStats *s) { s->tails--; }
static inline void synth_stats_pre_limiter(SynthStats *s,double l,double r) {
  s->maxPreLimiter=fmax(s->maxPreLimiter,fmax(fabs(l),fabs(r)));
}
static inline void synth_stats_sample(SynthStats *s,float l,float r,double gain,int rate) {
  s->minEverGain=fmin(s->minEverGain,gain);
  s->peakL=fmax(s->peakL,fabs(l)); s->peakR=fmax(s->peakR,fabs(r));
  s->sumL+=(double)l*l; s->sumR+=(double)r*r; s->minGain=fmin(s->minGain,gain);
  if(++s->windowFrames >= rate/5) {
    s->values[ST_PEAK_L]=synth_stats_db(s->peakL); s->values[ST_PEAK_R]=synth_stats_db(s->peakR);
    s->values[ST_RMS_L]=synth_stats_db(sqrt(s->sumL/s->windowFrames));
    s->values[ST_RMS_R]=synth_stats_db(sqrt(s->sumR/s->windowFrames));
    s->values[ST_REDUCTION]=-synth_stats_db(s->minGain);
    synth_stats_window(s);
  }
}
static inline void synth_stats_publish(SynthStats *s) {
  __atomic_fetch_add(&s->sequence,1,__ATOMIC_ACQ_REL);
  for(int i=0;i<SYNTH_STATS_COUNT;i++) synth_stats_store(&s->published[i],synth_stats_bits(s->values[i]));
  synth_stats_store(&s->publishedConfig,s->seenConfig); synth_stats_store(&s->publishedReset,s->seenReset);
  __atomic_fetch_add(&s->sequence,1,__ATOMIC_RELEASE);
}
static inline void synth_stats_end(SynthStats *s,int frames,int rate,double seconds) {
  s->values[ST_ACTIVE]=s->active; s->values[ST_PEAK_ACTIVE]=s->peakActive;
  s->values[ST_TAILS]=s->tails; s->values[ST_SPATIAL]=s->spatial; s->values[ST_STEALS]=(double)s->steals;
  s->values[ST_CALLBACK]=frames;
  if(s->enabled && frames>0) {
    s->values[ST_PRE_LIMITER_PEAK]=synth_stats_db(s->maxPreLimiter);
    s->values[ST_MAX_REDUCTION]=-synth_stats_db(s->minEverGain);
    double load=seconds*rate/frames; s->peakLoad=fmax(s->peakLoad,load);
    s->values[ST_LOAD]=load; s->values[ST_PEAK_LOAD]=s->peakLoad;
  }
  synth_stats_publish(s);
}
/* Call only after the backend has stopped its callback. Keeps accumulated maxima. */
static inline void synth_stats_stopped(SynthStats *s) {
  s->active=s->tails=s->spatial=0; synth_stats_window(s);
  for(int i=ST_LOAD;i<SYNTH_STATS_COUNT;i++) s->values[i]=NAN;
  bool enabled=s->enabled; s->enabled=false; synth_stats_end(s,0,1,0); s->enabled=enabled;
}
static inline void synth_stats_read(const SynthStats *s,double *out) {
  uint64_t before,after,c,r;
  do {
    before=synth_stats_load(&s->sequence); if(before&1) continue;
    for(int i=0;i<SYNTH_STATS_COUNT;i++) out[i]=synth_stats_double(synth_stats_load(&s->published[i]));
    c=synth_stats_load(&s->publishedConfig); r=synth_stats_load(&s->publishedReset);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    after=synth_stats_load(&s->sequence);
  } while((before&1) || before!=after);
  if(r!=synth_stats_load(&s->reset)) { out[ST_STEALS]=0; out[ST_PEAK_ACTIVE]=out[ST_ACTIVE]; }
  if(c!=synth_stats_load(&s->config) || !(c&1) || r!=synth_stats_load(&s->reset))
    for(int i=ST_LOAD;i<SYNTH_STATS_COUNT;i++) out[i]=NAN;
}
#endif
