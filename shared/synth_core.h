/*
 * DSP math shared by the Android (C++) and iOS (Objective-C) renderers.
 *
 * Plain C, header-only and allocation-free so both platforms compile the very
 * same code, and tests/dsp_core_regression.c checks it on the host.
 */
#ifndef SYNTH_CORE_H
#define SYNTH_CORE_H

#include <math.h>

#define SYNTH_PI 3.14159265358979323846

#define SYNTH_FILTER_NONE 0
#define SYNTH_FILTER_LOWPASS 1
#define SYNTH_FILTER_HIGHPASS 2
#define SYNTH_FILTER_BANDPASS 3

#define SYNTH_FILTER_MIN_HZ 20.0
#define SYNTH_FILTER_MIN_Q 0.5
#define SYNTH_FILTER_MAX_Q 10.0

static inline double synth_smoothstep(double x)
{
  double t = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
  return t * t * (3.0 - 2.0 * t);
}

/*
 * Event envelope in samples. With decay 0 and sustain 1 it is exactly the
 * original attack, flat, release shape. The release multiplies the attack and
 * decay stage, so a release that begins during the decay stays continuous.
 */
static inline double synth_envelope(int played, int duration, int attack, int decay,
                                    double sustain, int release)
{
  double base;
  if (attack > 0 && played < attack) {
    base = synth_smoothstep((double)played / (double)(attack - 1 > 1 ? attack - 1 : 1));
  } else if (decay > 0 && played < attack + decay) {
    base = 1.0 + (sustain - 1.0) *
                     synth_smoothstep((double)(played - attack) / (double)(decay - 1 > 1 ? decay - 1 : 1));
  } else {
    base = sustain;
  }
  int remaining = duration - played;
  if (release > 0 && remaining <= release) {
    base *= synth_smoothstep((double)(remaining - 1) / (double)(release - 1 > 1 ? release - 1 : 1));
  }
  return base;
}

/* Position of a sample inside a sweep that spans the whole event, 0 to 1. */
static inline double synth_sweep_progress(int played, int duration)
{
  if (duration <= 1) return 0.0;
  double t = (double)played / (double)(duration - 1);
  return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

/* Geometric interpolation: equal musical distance per unit of time. */
static inline double synth_geometric_at(double from, double to, double t)
{
  return from * pow(to / from, t);
}

static inline double synth_cents_ratio(double cents)
{
  return pow(2.0, cents / 1200.0);
}

/* Phase modulation of a sine carrier; phases are in cycles. */
static inline double synth_fm_sine(double phase, double modPhase, double depth)
{
  if (depth <= 0.0) return sin(2.0 * SYNTH_PI * phase);
  return sin(2.0 * SYNTH_PI * phase + depth * sin(2.0 * SYNTH_PI * modPhase));
}

/*
 * Zavalishin topology-preserving state-variable filter. Unlike a direct-form
 * biquad it stays stable while its coefficients move, so a cutoff sweep cannot
 * blow up. Lowpass peaks at Q at the cutoff; bandpass is normalised to unity
 * gain at its center for every Q.
 */
typedef struct {
  int type;
  double ic1, ic2;
  double a1, a2, a3, k;
} SynthFilter;

static inline void synth_filter_set(SynthFilter *f, double cutoffHz, double q, double sampleRate)
{
  double maxHz = sampleRate * 0.45;
  double fc = cutoffHz < SYNTH_FILTER_MIN_HZ ? SYNTH_FILTER_MIN_HZ : (cutoffHz > maxHz ? maxHz : cutoffHz);
  double safeQ = q < SYNTH_FILTER_MIN_Q ? SYNTH_FILTER_MIN_Q : (q > SYNTH_FILTER_MAX_Q ? SYNTH_FILTER_MAX_Q : q);
  double g = tan(SYNTH_PI * fc / sampleRate);
  f->k = 1.0 / safeQ;
  f->a1 = 1.0 / (1.0 + g * (g + f->k));
  f->a2 = g * f->a1;
  f->a3 = g * f->a2;
}

static inline void synth_filter_init(SynthFilter *f, int type)
{
  f->type = type;
  f->ic1 = 0.0;
  f->ic2 = 0.0;
  f->a1 = 1.0;
  f->a2 = 0.0;
  f->a3 = 0.0;
  f->k = 1.0;
}

/* Coefficients are refreshed on the first sample and every 8th while sweeping. */
static inline int synth_filter_update_due(int sweeping, int played)
{
  return played == 0 || (sweeping && (played & 7) == 0);
}

static inline void synth_filter_tick(SynthFilter *f, int sweeping, double cutoffHz, double q,
                                     double sampleRate, int played)
{
  if (synth_filter_update_due(sweeping, played)) synth_filter_set(f, cutoffHz, q, sampleRate);
}

static inline double synth_filter_process(SynthFilter *f, double x)
{
  if (f->type == SYNTH_FILTER_NONE) return x;
  double v3 = x - f->ic2;
  double v1 = f->a1 * f->ic1 + f->a2 * v3;
  double v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
  f->ic1 = 2.0 * v1 - f->ic1;
  f->ic2 = 2.0 * v2 - f->ic2;
  if (f->type == SYNTH_FILTER_LOWPASS) return v2;
  if (f->type == SYNTH_FILTER_BANDPASS) return f->k * v1;
  return x - f->k * v1 - v2;
}

#endif
