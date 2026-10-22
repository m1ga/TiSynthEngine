/* Public held-key retriggers may follow a compatible releasing oscillator.
 * Envelopes, gain, ownership and spatial histories always remain independent. */
#ifndef SYNTH_RETRIGGER_H
#define SYNTH_RETRIGGER_H
#include "synth_held.h"
typedef struct {
  int waveType, filterType, bitDepth, tableSize;
  double lowPassAlpha;
  const double *values, *table;
} SynthRetriggerKey;
static inline int synth_retrigger_compatible(const SynthLive *live,
    const SynthRetriggerKey *old, const SynthRetriggerKey *next) {
  if(old->waveType==4 || old->waveType!=next->waveType ||
     old->filterType!=next->filterType || old->bitDepth!=next->bitDepth ||
     old->lowPassAlpha!=next->lowPassAlpha) return 0;
  /* Stable live values avoid frequency/sample-rate roundtrip mismatches. */
  for(int i=0;i<SYNTH_LIVE_COUNT;i++) {
    if(live->remaining[i] || live->value[i]!=live->target[i]) return 0;
    if(i==LIVE_VOLUME) continue;
    if((i==LIVE_FILTER_HZ||i==LIVE_FILTER_Q)&&!old->filterType)continue;
    if(i==LIVE_PULSE_WIDTH&&old->waveType!=1)continue;
    if((i==LIVE_FM_RATIO||i==LIVE_FM_DEPTH)&&old->waveType!=0)continue;
    if(old->values[i]!=next->values[i])return 0;
  }
  if(old->waveType==5) {
    if(old->tableSize!=next->tableSize)return 0;
    for(int i=0;i<old->tableSize;i++)if(old->table[i]!=next->table[i])return 0;
  }
  return 1;
}
#endif
