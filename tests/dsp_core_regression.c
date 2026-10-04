/*
 * Host-side regression for shared/synth_core.h, the DSP math shared by the
 * Android and iOS renderers. Build and run:
 *
 *   cc -std=c99 -O2 -Wall -Wextra -o /tmp/dsp_core tests/dsp_core_regression.c -lm && /tmp/dsp_core
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "../shared/synth_core.h"

static int failures = 0;

#define CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
      failures++; \
    } \
  } while (0)

#define CHECK_NEAR(actual, expected, tolerance, message) \
  do { \
    double a_ = (actual), e_ = (expected); \
    if (!(fabs(a_ - e_) <= (tolerance))) { \
      fprintf(stderr, "FAIL: %s: got %.9g, expected %.9g +- %g (line %d)\n", \
              message, a_, e_, (double)(tolerance), __LINE__); \
      failures++; \
    } \
  } while (0)

/* The envelope both renderers used before decay and sustain existed. */
static double legacy_envelope(int played, int duration, int attack, int release)
{
  int remaining = duration - played;
  double envelope = 1.0;
  if (attack > 0 && played < attack)
    envelope = synth_smoothstep((double)played / (attack - 1 > 1 ? attack - 1 : 1));
  else if (release > 0 && remaining <= release)
    envelope = synth_smoothstep((double)(remaining - 1) / (release - 1 > 1 ? release - 1 : 1));
  return envelope;
}

static void test_envelope_matches_legacy_without_decay(void)
{
  const int durations[] = {1, 2, 3, 10, 100, 2400, 4800};
  long compared = 0;
  for (size_t di = 0; di < sizeof durations / sizeof durations[0]; di++) {
    int duration = durations[di];
    for (int attack = 0; attack < duration && attack <= 200; attack += (attack < 5 ? 1 : 7)) {
      for (int release = 0; release <= duration - attack && release <= 300; release += (release < 5 ? 1 : 11)) {
        for (int played = 0; played < duration; played++) {
          double expected = legacy_envelope(played, duration, attack, release);
          double actual = synth_envelope(played, duration, attack, 0, 1.0, release);
          if (actual != expected) {
            fprintf(stderr, "FAIL: envelope differs from legacy at D=%d a=%d r=%d p=%d: %.17g vs %.17g\n",
                    duration, attack, release, played, actual, expected);
            failures++;
            return;
          }
          compared++;
        }
      }
    }
  }
  CHECK(compared > 100000, "legacy envelope comparison covered a large grid");
}

static void test_decay_and_sustain(void)
{
  const int D = 2000, a = 100, d = 400, r = 200;
  const double s = 0.3;
  CHECK_NEAR(synth_envelope(a, D, a, d, s, r), 1.0, 1e-12, "level at the end of the attack is 1");
  CHECK_NEAR(synth_envelope(a + d - 1, D, a, d, s, r), s, 1e-12, "level at the end of the decay is the sustain");
  CHECK_NEAR(synth_envelope(a + d + 500, D, a, d, s, r), s, 1e-12, "level holds at the sustain");
  CHECK_NEAR(synth_envelope(D - 1, D, a, d, s, r), 0.0, 1e-12, "last sample reaches zero");
  double previous = 1.0;
  for (int p = a; p < a + d; p++) {
    double level = synth_envelope(p, D, a, d, s, r);
    CHECK(level <= previous + 1e-15, "decay never rises");
    previous = level;
  }
  double sustained = synth_envelope(D - r - 1, D, a, d, s, r);
  CHECK_NEAR(sustained, s, 1e-12, "release starts from the sustain level");
}

static void test_release_may_cut_the_decay(void)
{
  /* Release begins at sample 60 while the decay (100 samples) is still running. */
  const int D = 120, a = 0, d = 100, r = 60;
  double previous = synth_envelope(0, D, a, d, 0.5, r);
  double largestStep = 0.0;
  for (int p = 1; p < D; p++) {
    double level = synth_envelope(p, D, a, d, 0.5, r);
    double step = fabs(level - previous);
    if (step > largestStep) largestStep = step;
    CHECK(level >= 0.0 && level <= 1.0, "envelope stays within 0 and 1");
    previous = level;
  }
  CHECK(largestStep < 0.05, "no jump where the release interrupts the decay");
  CHECK_NEAR(synth_envelope(D - 1, D, a, d, 0.5, r), 0.0, 1e-12, "interrupted decay still ends at zero");
}

static double sine_gain(int type, double cutoff, double q, double frequency, double sampleRate)
{
  SynthFilter filter;
  synth_filter_init(&filter, type);
  synth_filter_set(&filter, cutoff, q, sampleRate);
  const int settle = (int)(sampleRate * 0.25), measure = (int)(sampleRate * 0.25);
  double phase = 0.0, increment = frequency / sampleRate, peak = 0.0;
  for (int i = 0; i < settle + measure; i++) {
    double y = synth_filter_process(&filter, sin(2.0 * SYNTH_PI * phase));
    phase += increment;
    if (i >= settle && fabs(y) > peak) peak = fabs(y);
  }
  return peak;
}

static void test_filter_responses(void)
{
  const double sr = 48000.0;
  CHECK_NEAR(sine_gain(SYNTH_FILTER_LOWPASS, 1000.0, 0.7071, 100.0, sr), 1.0, 0.02, "lowpass passes a low tone");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_LOWPASS, 1000.0, 0.7071, 1000.0, sr), 0.7071, 0.02, "lowpass is 3 dB down at the cutoff");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_LOWPASS, 1000.0, 0.7071, 8000.0, sr), 1.0 / 64.0, 0.006, "lowpass falls 12 dB per octave");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_HIGHPASS, 1000.0, 0.7071, 10000.0, sr), 1.0, 0.03, "highpass passes a high tone");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_HIGHPASS, 1000.0, 0.7071, 1000.0, sr), 0.7071, 0.02, "highpass is 3 dB down at the cutoff");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_HIGHPASS, 1000.0, 0.7071, 125.0, sr), 1.0 / 64.0, 0.006, "highpass falls 12 dB per octave");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_BANDPASS, 1000.0, 4.0, 1000.0, sr), 1.0, 0.02, "bandpass has unity gain at its center for any Q");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_BANDPASS, 1000.0, 10.0, 1000.0, sr), 1.0, 0.02, "bandpass center gain does not grow with Q");
  CHECK(sine_gain(SYNTH_FILTER_BANDPASS, 1000.0, 4.0, 100.0, sr) < 0.12, "bandpass rejects a distant low tone");
  CHECK(sine_gain(SYNTH_FILTER_BANDPASS, 1000.0, 4.0, 10000.0, sr) < 0.12, "bandpass rejects a distant high tone");
  CHECK_NEAR(sine_gain(SYNTH_FILTER_LOWPASS, 1000.0, 8.0, 1000.0, sr), 8.0, 0.4, "lowpass resonance peaks at Q");
}

static void test_filter_stays_stable_while_swept(void)
{
  const double sr = 48000.0;
  const double qs[] = {0.5, 2.0, 10.0};
  const int types[] = {SYNTH_FILTER_LOWPASS, SYNTH_FILTER_HIGHPASS, SYNTH_FILTER_BANDPASS};
  uint32_t state = 12345u;
  for (size_t qi = 0; qi < 3; qi++) {
    for (size_t ti = 0; ti < 3; ti++) {
      SynthFilter filter;
      synth_filter_init(&filter, types[ti]);
      const int duration = 96000;
      double largest = 0.0;
      for (int p = 0; p < duration; p++) {
        double t = p < duration / 2 ? (double)p / (duration / 2 - 1) : (double)(duration - 1 - p) / (duration / 2 - 1);
        double cutoff = synth_geometric_at(20.0, sr * 0.45, t);
        synth_filter_tick(&filter, 1, cutoff, qs[qi], sr, p);
        state = state * 1664525u + 1013904223u;
        double noise = ((double)(int32_t)state) / 2147483648.0;
        double y = synth_filter_process(&filter, noise);
        if (!isfinite(y)) { CHECK(0, "swept filter output is finite"); return; }
        if (fabs(y) > largest) largest = fabs(y);
      }
      CHECK(largest < 60.0, "swept filter output stays bounded");
    }
  }
}

static void test_filter_sweep_helpers(void)
{
  CHECK_NEAR(synth_geometric_at(100.0, 6400.0, 0.5), 800.0, 1e-9, "geometric midpoint of a cutoff sweep");
  CHECK_NEAR(synth_geometric_at(100.0, 6400.0, 0.0), 100.0, 1e-12, "sweep starts at the start cutoff");
  CHECK_NEAR(synth_geometric_at(100.0, 6400.0, 1.0), 6400.0, 1e-9, "sweep ends at the end cutoff");
  CHECK_NEAR(synth_sweep_progress(0, 1000), 0.0, 1e-15, "progress starts at zero");
  CHECK_NEAR(synth_sweep_progress(999, 1000), 1.0, 1e-15, "progress ends at one");
  CHECK_NEAR(synth_sweep_progress(0, 1), 0.0, 1e-15, "one-sample event has no progress");
}

static double goertzel_amplitude(const double *signal, int n, double sampleRate, double frequency)
{
  double re = 0.0, im = 0.0;
  for (int i = 0; i < n; i++) {
    double angle = 2.0 * SYNTH_PI * frequency * i / sampleRate;
    re += signal[i] * cos(angle);
    im += signal[i] * sin(angle);
  }
  return 2.0 * sqrt(re * re + im * im) / n;
}

static void test_fm_sidebands_follow_bessel_functions(void)
{
  const double sr = 48000.0, carrier = 4800.0;
  const int n = 48000;
  double *signal = malloc(sizeof(double) * n);
  CHECK(signal != NULL, "allocate fm buffer");
  if (!signal) return;

  /* Index 1 and 2, modulator at a tenth of the carrier (480 Hz), far from the Nyquist fold. Known Bessel values. */
  const double depths[] = {1.0, 2.0};
  const double j0[] = {0.76519768655797, 0.22389077914124};
  const double j1[] = {0.44005058574493, 0.57672480775687};
  for (int c = 0; c < 2; c++) {
    double phase = 0.0, modPhase = 0.0, increment = carrier / sr;
    for (int i = 0; i < n; i++) {
      signal[i] = synth_fm_sine(phase, modPhase, depths[c]);
      phase += increment;
      modPhase += increment * 0.1;
    }
    CHECK_NEAR(goertzel_amplitude(signal, n, sr, carrier), j0[c], 0.005, "carrier amplitude is J0(index)");
    CHECK_NEAR(goertzel_amplitude(signal, n, sr, carrier + 480.0), j1[c], 0.005, "upper sideband is J1(index)");
    CHECK_NEAR(goertzel_amplitude(signal, n, sr, carrier - 480.0), j1[c], 0.005, "lower sideband is J1(index)");
  }
  free(signal);

  for (int i = 0; i < 1000; i++) {
    double phase = i * 0.0137;
    CHECK(synth_fm_sine(phase, i * 0.31, 0.0) == sin(2.0 * SYNTH_PI * phase), "zero depth is exactly the plain sine");
  }
}

static void test_detune_ratio(void)
{
  CHECK_NEAR(synth_cents_ratio(0.0), 1.0, 0.0, "0 cents leaves the pitch alone");
  CHECK_NEAR(synth_cents_ratio(1200.0), 2.0, 1e-12, "1200 cents is one octave up");
  CHECK_NEAR(synth_cents_ratio(-1200.0), 0.5, 1e-12, "-1200 cents is one octave down");
  CHECK_NEAR(synth_cents_ratio(100.0), 1.0594630943593, 1e-12, "100 cents is one semitone");
  CHECK_NEAR(440.0 * synth_cents_ratio(-5.0), 438.731063, 0.0001, "-5 cents on A4");
}

int main(void)
{
  test_envelope_matches_legacy_without_decay();
  test_decay_and_sustain();
  test_release_may_cut_the_decay();
  test_filter_responses();
  test_filter_stays_stable_while_swept();
  test_filter_sweep_helpers();
  test_fm_sidebands_follow_bessel_functions();
  test_detune_ratio();
  if (failures) {
    fprintf(stderr, "dsp_core_regression: %d failure(s)\n", failures);
    return 1;
  }
  printf("dsp_core_regression: all tests passed\n");
  return 0;
}
