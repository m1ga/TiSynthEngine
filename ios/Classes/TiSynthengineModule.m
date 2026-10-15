#import "TiSynthengineModule.h"
#import "TiSynthengineContract.h"
#import "TiBase.h"
#import "TiHost.h"
#import "TiUtils.h"
#import <AVFoundation/AVFoundation.h>
#import <limits.h>
#import <math.h>
#import <mach/mach_time.h>
#import <os/lock.h>
#import <stdatomic.h>
#import <stdint.h>
#include "../../shared/synth_core.h"
#include "../../shared/synth_spatial.h"
#include "../../shared/synth_stats.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define DEFAULT_SAMPLE_RATE   44100.0
#define ABSOLUTE_MAX_VOICES   32
#define SEQ_MAX_STEPS         32
#define WAVETABLE_MAX_SAMPLES 64

#define OUTPUT_CEILING        0.85
#define SPEAKER_SAFE_OUTPUT_CEILING 0.70
#define MIXING_PROFILE_RAW          0
#define MIXING_PROFILE_CONSERVATIVE 1
#define MIXING_PROFILE_BALANCED     2
#define MIXING_PROFILE_SPEAKER_SAFE 3
#define CONSERVATIVE_SINE_GAIN      0.44
#define STOP_ALL_FADE_MS      20
#define VOICE_STEAL_FADE_MS   6
#define PATTERN_MAX_STEPS     512

#define COMMAND_QUEUE_CAPACITY 128

#define DEFAULT_FREQUENCY_HZ  440.0

#define DURATION_DEFAULT_MS   500
#define DURATION_MIN_MS       1
#define DURATION_MAX_MS       60000

#define ATTACK_DEFAULT_MS     50
#define ATTACK_MIN_MS         0
#define ATTACK_MAX_MS         2000

#define RELEASE_DEFAULT_MS    50
#define RELEASE_MIN_MS        0
#define RELEASE_MAX_MS        4000

#define VOLUME_DEFAULT        1.0
#define VOLUME_MIN            0.0
#define VOLUME_MAX            2.0

#define MIN_FREQUENCY_HZ      1.0

// ---------------------------------------------------------------------------
// Voice struct
// ---------------------------------------------------------------------------

typedef struct {
  bool   isActive;
  bool spatial;
  SynthSpatial spatialState;
  int spatialDrain;
  double phase;
  double phaseIncrement;
  double phaseIncrDelta;
  int    totalSamples;
  int    samplesPlayed;
  int    attackSamples;
  int    releaseSamples;
  double volume;
  double currentEnvelope;

  // Timbre: 0=sine 1=square 2=sawtooth 3=triangle 4=noise 5=wavetable
  int    waveType;

  // Stereo panning
  double pan;
  double panDelta;
  double leftGain;
  double rightGain;

  // LFO vibrato
  double lfoPhase;
  double lfoPhaseIncrement;
  double lfoDepth;

  // Native sequencer
  double seqFreqs[SEQ_MAX_STEPS];
  int    seqStepCount;
  int    seqCurrentStep;
  int    seqStepSamples;
  int    seqSamplesInStep;

  // Noise LCG
  uint64_t noiseState;
  double noisePhase;
  double noisePhaseIncrement;
  double noisePhaseIncrDelta;
  double heldNoise;

  // Optional retro character controls
  double filterState;
  double filterAlpha;
  double pulseWidth;
  double pulseWidthDelta;
  int bitDepth;
  double wavetable[WAVETABLE_MAX_SAMPLES];
  int wavetableSize;

  // Decay and sustain stages of the envelope
  int    decaySamples;
  double sustain;

  // State-variable filter; the cutoff moves from svfHz to svfHzEnd when sweeping
  SynthFilter svf;
  bool   svfSweeping;
  double svfHz;
  double svfHzEnd;
  double svfQ;

  // Phase-modulated sine
  bool   fmActive;
  double fmRatio;
  double fmModPhase;
  double fmDepth;
  double fmDepthDelta;
} SynthVoice;

typedef struct {
  bool isActive;
  bool spatial;
  SynthSpatial spatialState;
  int spatialDrain;
  double phase;
  double phaseIncrement;
  double volume;
  int waveType;
  double leftGain;
  double rightGain;
  uint64_t noiseState;
  double noisePhase;
  double noisePhaseIncrement;
  double heldNoise;
  double filterState;
  double filterAlpha;
  double pulseWidth;
  int bitDepth;
  double wavetable[WAVETABLE_MAX_SAMPLES];
  int wavetableSize;
  SynthFilter svf;
  bool fmActive;
  double fmRatio;
  double fmModPhase;
  double fmDepth;
  int totalSamples;
  int samplesRemaining;
} TailVoice;

// ---------------------------------------------------------------------------
// Command struct
// ---------------------------------------------------------------------------

typedef struct {
  int    type;
  BOOL spatial;
  double spatialX, spatialY;
  double frequency;
  int    durationSamples;
  int    attackSamples;
  int    releaseSamples;
  double volume;
  int    eventVoiceCount;
  int    waveType;
  double pan;
  double panEnd;
  double frequencyEnd;
  double lfoFreq;
  double lfoDepth;
  double noiseRate;
  double noiseRateEnd;
  double lowPassHz;
  double pulseWidth;
  double pulseWidthEnd;
  int bitDepth;
  double wavetable[WAVETABLE_MAX_SAMPLES];
  int wavetableSize;
  double seqFreqs[SEQ_MAX_STEPS];
  int    seqStepCount;
  int    seqStepDurationMs;
  int    decaySamples;
  double sustain;
  int    filterType;
  double filterHz;
  double filterHzEnd;
  double filterQ;
  double fmRatio;
  double fmDepth;
  double fmDepthEnd;
  bool   hasFmDepthEnd;
} SynthCommand;

enum {
  CMD_NOTE_ON  = 1,
  CMD_STOP_ALL = 2,
  CMD_SHUTDOWN = 3
};

// ---------------------------------------------------------------------------
// C utilities
// ---------------------------------------------------------------------------

static double ClampDouble(double value, double minValue, double maxValue)
{
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static int ClampInt(int value, int minValue, int maxValue)
{
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static int MsToSamples(int ms, double sampleRate)
{
  return (int)((ms / 1000.0) * sampleRate);
}

static double SmoothStep01(double x)
{
  double t = ClampDouble(x, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

static double VoiceGainForEvent(int profile, int waveType, int voiceCount)
{
  if (profile == MIXING_PROFILE_RAW) return 1.0;

  double gain;
  if (waveType == 1) {
    gain = CONSERVATIVE_SINE_GAIN / M_SQRT2;
  } else if (waveType == 0) {
    gain = CONSERVATIVE_SINE_GAIN;
  } else {
    gain = CONSERVATIVE_SINE_GAIN * sqrt(1.5);
  }
  if (profile == MIXING_PROFILE_BALANCED) {
    gain *= M_SQRT2;
  } else if (profile == MIXING_PROFILE_SPEAKER_SAFE) {
    gain /= M_SQRT2;
  }
  return gain / sqrt((double)MAX(1, voiceCount));
}

static double PolyBlep(double t, double dt)
{
  if (t < dt) {
    double x = t / dt;
    return x + x - x * x - 1.0;
  }
  if (t > 1.0 - dt) {
    double x = (t - 1.0) / dt;
    return x * x + x + x + 1.0;
  }
  return 0.0;
}

static double RenderPeriodicWave(int waveType, double phase, double phaseIncrement,
                                 double pulseWidth)
{
  double t = phase - floor(phase);
  double dt = ClampDouble(fabs(phaseIncrement), 1.0e-9, 0.5);
  switch (waveType) {
    case 1: {
      double width = ClampDouble(pulseWidth, 0.05, 0.95);
      double value = t < width ? 1.0 : -1.0;
      value += PolyBlep(t, dt);
      double shifted = t - width;
      if (shifted < 0.0) shifted += 1.0;
      value -= PolyBlep(shifted, dt);
      return value;
    }
    case 2:
      return 2.0 * t - 1.0 - PolyBlep(t, dt);
    case 3:
      return 1.0 - 4.0 * fabs(t - 0.5);
    default:
      return sin(t * 2.0 * M_PI);
  }
}

static double QuantizeWave(double value, int bitDepth)
{
  if (bitDepth < 2) return value;
  int safeDepth = ClampInt(bitDepth, 2, 16);
  double levels = (double)((1u << safeDepth) - 1u);
  double normalized = ClampDouble((value + 1.0) * 0.5, 0.0, 1.0);
  return round(normalized * levels) / levels * 2.0 - 1.0;
}

static double RenderWavetable(double phase, const double *table, int tableSize)
{
  if (tableSize < 2) return sin((phase - floor(phase)) * 2.0 * M_PI);
  double t = phase - floor(phase);
  int index = MIN(tableSize - 1, (int)(t * tableSize));
  return table[index];
}

// splitmix64-style noise: returns a value in [-1, 1]
static double LcgNoise(uint64_t *state)
{
  *state += 0x9e3779b97f4a7c15ULL;
  uint64_t z = *state;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  z ^= (z >> 31);
  return (double)(int64_t)z / (double)0x7FFFFFFFFFFFFFFFLL;
}

static BOOL ParseFiniteDoubleFromAny(id value, double *outValue)
{
  if ([value isKindOfClass:[NSNumber class]]) {
    double parsed = [(NSNumber *)value doubleValue];
    if (!isfinite(parsed)) return NO;
    *outValue = parsed;
    return YES;
  }
  if ([value isKindOfClass:[NSString class]]) {
    NSScanner *scanner = [NSScanner scannerWithString:(NSString *)value];
    double parsed = 0.0;
    if (![scanner scanDouble:&parsed]) return NO;
    if (![scanner isAtEnd] || !isfinite(parsed)) return NO;
    *outValue = parsed;
    return YES;
  }
  return NO;
}

static BOOL ParseStrictIntegerFromAny(id value, NSInteger *outValue)
{
  double parsed = 0.0;
  if (!ParseFiniteDoubleFromAny(value, &parsed)) return NO;
  double rounded = nearbyint(parsed);
  if (fabs(parsed - rounded) > 1e-9) return NO;
  if (rounded < (double)INT_MIN || rounded > (double)INT_MAX) return NO;
  *outValue = (NSInteger)rounded;
  return YES;
}

static void CopyWaveTableFromOptions(NSDictionary *options,
                                     const double *fallback, int fallbackSize,
                                     double *destination, int *outSize)
{
  int count = ClampInt(fallbackSize, 0, WAVETABLE_MAX_SAMPLES);
  if (count > 0 && fallback != NULL) {
    memcpy(destination, fallback, count * sizeof(double));
  }
  id raw = [options objectForKey:@"waveTable"];
  if ([raw isKindOfClass:[NSArray class]]) {
    NSArray *values = (NSArray *)raw;
    count = MIN((int)values.count, WAVETABLE_MAX_SAMPLES);
    for (int i = 0; i < count; i++) {
      double value = 0.0;
      if (ParseFiniteDoubleFromAny(values[i], &value)) {
        destination[i] = ClampDouble(value, -1.0, 1.0);
      } else {
        destination[i] = 0.0;
      }
    }
  }
  *outSize = count;
}

// ---------------------------------------------------------------------------
// Note name parser
// Supports: C4, F#4, Bb3, Do4, Fa#4, Sib3 → returns -1 on failure
// ---------------------------------------------------------------------------
static double ParseNoteToFrequency(NSString *noteName)
{
  if (noteName == nil || noteName.length == 0) return -1.0;
  NSString *s     = [noteName stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceCharacterSet]];
  NSString *upper = [s uppercaseString];
  NSUInteger pos  = 0;
  int semitone    = -1;

  if ([upper hasPrefix:@"SOL"])     { semitone = 7;  pos = 3; }
  else if ([upper hasPrefix:@"DO"]) { semitone = 0;  pos = 2; }
  else if ([upper hasPrefix:@"RE"]) { semitone = 2;  pos = 2; }
  else if ([upper hasPrefix:@"MI"]) { semitone = 4;  pos = 2; }
  else if ([upper hasPrefix:@"FA"]) { semitone = 5;  pos = 2; }
  else if ([upper hasPrefix:@"LA"]) { semitone = 9;  pos = 2; }
  else if ([upper hasPrefix:@"SI"]) { semitone = 11; pos = 2; }
  else if ([upper hasPrefix:@"TI"]) { semitone = 11; pos = 2; }
  else {
    pos = 1;
    if (s.length == 0) return -1.0;
    unichar c = [upper characterAtIndex:0];
    switch (c) {
      case 'C': semitone = 0;  break;
      case 'D': semitone = 2;  break;
      case 'E': semitone = 4;  break;
      case 'F': semitone = 5;  break;
      case 'G': semitone = 7;  break;
      case 'A': semitone = 9;  break;
      case 'B': semitone = 11; break;
      default:  return -1.0;
    }
  }

  // Accidental (use original string: lowercase 'b' = flat)
  if (pos < s.length) {
    unichar acc = [s characterAtIndex:pos];
    if (acc == '#') { semitone++; pos++; }
    else if (acc == 'b') { semitone--; pos++; }
  }

  // Octave digit, default 4
  int octave = 4;
  if (pos < s.length) {
    unichar c = [s characterAtIndex:pos];
    if (c >= '0' && c <= '9') octave = (int)(c - '0');
  }

  int midi = (octave + 1) * 12 + semitone;
  return 440.0 * pow(2.0, (midi - 69.0) / 12.0);
}

// Note value → duration in milliseconds
static double ComputeNoteValueMs(NSString *nv, double quarterMs)
{
  if (nv == nil) return quarterMs;
  if ([nv isEqualToString:@"1n"])  return quarterMs * 4.0;
  if ([nv isEqualToString:@"2n"])  return quarterMs * 2.0;
  if ([nv isEqualToString:@"2n."]) return quarterMs * 3.0;
  if ([nv isEqualToString:@"4n"])  return quarterMs;
  if ([nv isEqualToString:@"4n."]) return quarterMs * 1.5;
  if ([nv isEqualToString:@"8n"])  return quarterMs * 0.5;
  if ([nv isEqualToString:@"8n."]) return quarterMs * 0.75;
  if ([nv isEqualToString:@"8t"])  return quarterMs / 3.0;
  if ([nv isEqualToString:@"16n"]) return quarterMs * 0.25;
  if ([nv isEqualToString:@"16t"]) return quarterMs / 6.0;
  return quarterMs;
}

// ---------------------------------------------------------------------------
// Expressive controls: decay, sustain, filter, FM and detune. Options are
// already validated; a step reads its own keys over the values it inherits.
// ---------------------------------------------------------------------------

typedef struct {
  NSInteger decayMs;
  double sustain;
  int    filterType;
  double filterHz;
  double filterHzEnd;
  double filterQ;
  double fmRatio;
  double fmDepth;
  double fmDepthEnd;
  BOOL   hasFmDepthEnd;
  double detune;
  BOOL spatial;
  double spatialX, spatialY;
} ExpressiveParams;

static ExpressiveParams ExpressiveDefaults(void)
{
  ExpressiveParams p;
  p.decayMs = 0;
  p.sustain = 1.0;
  p.filterType = SYNTH_FILTER_NONE;
  p.filterHz = 1000.0;
  p.filterHzEnd = 0.0;
  p.filterQ = 0.7071;
  p.fmRatio = 1.0;
  p.fmDepth = 0.0;
  p.fmDepthEnd = 0.0;
  p.hasFmDepthEnd = NO;
  p.detune = 0.0;
  p.spatial = NO; p.spatialX = p.spatialY = 0.0;
  return p;
}

static int FilterTypeFromString(NSString *raw)
{
  NSString *value = [[raw stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
  if ([value isEqualToString:@"lowpass"]) return SYNTH_FILTER_LOWPASS;
  if ([value isEqualToString:@"highpass"]) return SYNTH_FILTER_HIGHPASS;
  if ([value isEqualToString:@"bandpass"]) return SYNTH_FILTER_BANDPASS;
  return SYNTH_FILTER_NONE;
}

static ExpressiveParams ReadExpressiveParams(NSDictionary *options, ExpressiveParams inherited)
{
  ExpressiveParams p = inherited;
  double v = 0.0;
  if (ParseFiniteDoubleFromAny([options objectForKey:@"spatialX"], &v)) { p.spatialX = v; p.spatial = YES; }
  if (ParseFiniteDoubleFromAny([options objectForKey:@"spatialY"], &v)) { p.spatialY = v; p.spatial = YES; }
  if (ParseFiniteDoubleFromAny([options objectForKey:@"decay"], &v)) p.decayMs = (NSInteger)llround(v);
  if (ParseFiniteDoubleFromAny([options objectForKey:@"sustain"], &v)) p.sustain = v;
  id filterType = [options objectForKey:@"filterType"];
  if ([filterType isKindOfClass:[NSString class]]) p.filterType = FilterTypeFromString(filterType);
  if (ParseFiniteDoubleFromAny([options objectForKey:@"filterHz"], &v)) p.filterHz = v;
  if (ParseFiniteDoubleFromAny([options objectForKey:@"filterHzEnd"], &v)) p.filterHzEnd = v;
  if (ParseFiniteDoubleFromAny([options objectForKey:@"filterQ"], &v)) p.filterQ = v;
  if (ParseFiniteDoubleFromAny([options objectForKey:@"fmRatio"], &v)) p.fmRatio = v;
  if (ParseFiniteDoubleFromAny([options objectForKey:@"fmDepth"], &v)) p.fmDepth = v;
  if (ParseFiniteDoubleFromAny([options objectForKey:@"fmDepthEnd"], &v)) {
    p.fmDepthEnd = v;
    p.hasFmDepthEnd = YES;
  }
  if (ParseFiniteDoubleFromAny([options objectForKey:@"detune"], &v)) p.detune = v;
  return p;
}

static void ApplyExpressiveParams(SynthCommand *cmd, const ExpressiveParams *p, double sampleRate)
{
  cmd->decaySamples  = MAX(0, MsToSamples((int)p->decayMs, sampleRate));
  cmd->sustain       = p->sustain;
  cmd->filterType    = p->filterType;
  cmd->filterHz      = p->filterHz;
  cmd->filterHzEnd   = p->filterHzEnd;
  cmd->filterQ       = p->filterQ;
  cmd->fmRatio       = p->fmRatio;
  cmd->fmDepth       = p->fmDepth;
  cmd->fmDepthEnd    = p->fmDepthEnd;
  cmd->hasFmDepthEnd = p->hasFmDepthEnd;
  cmd->spatial = p->spatial;
  cmd->spatialX = p->spatialX; cmd->spatialY = p->spatialY;
}

static double DetunedFrequency(double frequency, double cents, double maxFrequency)
{
  if (cents == 0.0) return frequency;
  return ClampDouble(frequency * synth_cents_ratio(cents), MIN_FREQUENCY_HZ, maxFrequency);
}

// ---------------------------------------------------------------------------
// Module interface
// ---------------------------------------------------------------------------

@interface TiSynthengineModule () {
  SynthStats stats;
  uint64_t statsRejected[3], droppedPatternSteps;
  BOOL statsEnabled;
  mach_timebase_info_data_t statsTimebase;
  AVAudioEngine      *audioEngine;
  AVAudioSourceNode  *sourceNode;

  int    engineMaxVoices;
  double engineSampleRate;

  SynthVoice    voices[ABSOLUTE_MAX_VOICES];
  TailVoice     tails[ABSOLUTE_MAX_VOICES];
  SynthCommand  commandQueue[COMMAND_QUEUE_CAPACITY];

  _Atomic(uint32_t) commandWriteIndex;
  _Atomic(uint32_t) commandReadIndex;
  os_unfair_lock commandProducerLock;

  int    stopFadeSamples;
  int    stealFadeSamples;
  _Atomic(bool) emergencyStopRequested;
  _Atomic(bool) emergencyShutdownRequested;

  // Atomic master volume: written by main thread, read by audio thread
  _Atomic(double) masterVolume;

  // Pattern cancellation: incremented each time playPattern is called or shutdown
  _Atomic(uint32_t) patternGeneration;

  // Generation of the pattern whose patternend event is still owed; 0 when none
  _Atomic(uint32_t) pendingPatternGeneration;

  NSMutableSet *warningKeys;

  // Peak limiter state (render-thread only)
  double smoothedMasterGain;
  double masterGainTarget;
  double masterGainStep;
  int masterGainFramesRemaining;
  double smoothedGain;
  int mixingProfile;
  double gainReleaseCoeff;
  double limiterAttackTarget;
  double limiterAttackStep;

  // Lookahead buffer: delays audio so gain settles before the signal is output
  double *lookaheadL;
  double *lookaheadR;
  double *lookaheadPeaks;
  int64_t *lookaheadPeakIndices;
  int    lookaheadSize;
  int    lookaheadIdx;
  int    lookaheadPeakHead;
  int    lookaheadPeakCount;
  int64_t limiterSampleIndex;

  BOOL observingAudioNotifications;
  AVAudioSessionCategory audioSessionCategory;
}
@end

@implementation TiSynthengineModule

- (id)moduleGUID
{
  return @"ee6624e0-d373-4cf9-8849-a1f034594518";
}

- (NSString *)moduleId
{
  return @"ti.synthengine";
}

- (void)startup
{
  [super startup];
  synth_stats_init(&stats);
  mach_timebase_info(&statsTimebase);
  warningKeys = [[NSMutableSet alloc] init];
  engineSampleRate = DEFAULT_SAMPLE_RATE;
  engineMaxVoices  = 8;
  atomic_store_explicit(&commandWriteIndex, 0, memory_order_relaxed);
  atomic_store_explicit(&commandReadIndex,  0, memory_order_relaxed);
  commandProducerLock = OS_UNFAIR_LOCK_INIT;
  atomic_store_explicit(&masterVolume,      1.0, memory_order_relaxed);
  atomic_store_explicit(&patternGeneration, 0, memory_order_relaxed);
  atomic_store_explicit(&pendingPatternGeneration, 0, memory_order_relaxed);
  atomic_store_explicit(&emergencyStopRequested, false, memory_order_relaxed);
  atomic_store_explicit(&emergencyShutdownRequested, false, memory_order_relaxed);
  if (!observingAudioNotifications) {
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:self selector:@selector(audioRouteDidChange:)
                   name:AVAudioSessionRouteChangeNotification object:nil];
    [center addObserver:self selector:@selector(audioEngineConfigurationDidChange:)
                   name:AVAudioEngineConfigurationChangeNotification object:nil];
    observingAudioNotifications = YES;
  }
  DebugLog(@"[DEBUG] %@ loaded", self);
}

- (void)resumeAudioEngineAfterRouteChange:(NSString *)reason
{
  dispatch_async(dispatch_get_main_queue(), ^{
    @synchronized(self) {
    if (self->audioEngine == nil || [self->audioEngine isRunning]) return;

    NSError *sessionError = nil;
    AVAudioSession *session = [AVAudioSession sharedInstance];
    if (self->audioSessionCategory != nil
        && ![session.category isEqualToString:self->audioSessionCategory]) {
      NSError *categoryError = nil;
      [session setCategory:self->audioSessionCategory error:&categoryError];
      if (categoryError != nil) {
        DebugLog(@"[ERROR] Unable to reapply audio category after %@: %@",
                 reason, categoryError.localizedDescription);
      }
    }
    [session setActive:YES error:&sessionError];
    if (sessionError != nil) {
      DebugLog(@"[ERROR] Unable to reactivate audio after %@: %@",
               reason, sessionError.localizedDescription);
      return;
    }

    NSError *engineError = nil;
    BOOL started = [self->audioEngine startAndReturnError:&engineError];
    if (!started || engineError != nil) {
      DebugLog(@"[ERROR] Unable to resume audio after %@: %@",
               reason, engineError.localizedDescription);
    }
    }
  });
}

- (void)audioRouteDidChange:(NSNotification *)notification
{
#pragma unused(notification)
  [self resumeAudioEngineAfterRouteChange:@"route change"];
}

- (void)audioEngineConfigurationDidChange:(NSNotification *)notification
{
#pragma unused(notification)
  [self resumeAudioEngineAfterRouteChange:@"engine configuration change"];
}

// ---------------------------------------------------------------------------
#pragma mark - Warning helpers
// ---------------------------------------------------------------------------

- (void)warnOnceMethod:(NSString *)method key:(NSString *)key reason:(NSString *)reason
                 value:(id)value action:(NSString *)action
{
  if (warningKeys == nil) warningKeys = [[NSMutableSet alloc] init];
  NSString *onceKey = [NSString stringWithFormat:@"%@:%@:%@", method, key, reason];
  @synchronized(warningKeys) {
    if ([warningKeys containsObject:onceKey]) return;
    [warningKeys addObject:onceKey];
  }
  DebugLog(@"[WARN] event=validation_warning method=%@ key=%@ reason=%@ action=%@ value=%@",
           method, key, reason, action, value);
}

- (BOOL)acceptValidation:(NSDictionary *)failure method:(NSString *)method
{
  if (failure == nil) return YES;
  __atomic_fetch_add(&statsRejected[0],1,__ATOMIC_RELAXED);
  [self warnOnceMethod:method
                   key:[failure objectForKey:@"key"]
                reason:[failure objectForKey:@"reason"]
                 value:[failure objectForKey:@"value"]
                action:@"rejected"];
  return NO;
}

- (BOOL)containsRawKey:(NSDictionary *)dict key:(NSString *)key
{
  return (dict != nil) && ([dict objectForKey:key] != nil);
}

- (double)maxFrequencyHz
{
  double sr = isfinite(engineSampleRate) && engineSampleRate > 0.0
            ? engineSampleRate : DEFAULT_SAMPLE_RATE;
  return sr * 0.45;
}

// ---------------------------------------------------------------------------
#pragma mark - Option readers
// ---------------------------------------------------------------------------

- (BOOL)readRequiredDoubleFrom:(NSDictionary *)options method:(NSString *)method
                           key:(NSString *)key out:(double *)outValue
{
  if (![self containsRawKey:options key:key]) {
    [self warnOnceMethod:method key:key reason:@"missing_required" value:@"null" action:@"ignored"];
    return NO;
  }
  id raw = [options objectForKey:key];
  double parsed = 0.0;
  if (!ParseFiniteDoubleFromAny(raw, &parsed)) {
    [self warnOnceMethod:method key:key reason:@"non_numeric_or_non_finite" value:raw action:@"ignored"];
    return NO;
  }
  *outValue = parsed;
  return YES;
}

- (BOOL)readRequiredIntegerFrom:(NSDictionary *)options method:(NSString *)method
                            key:(NSString *)key out:(NSInteger *)outValue
{
  if (![self containsRawKey:options key:key]) {
    [self warnOnceMethod:method key:key reason:@"missing_required" value:@"null" action:@"ignored"];
    return NO;
  }
  id raw = [options objectForKey:key];
  NSInteger parsed = 0;
  if (!ParseStrictIntegerFromAny(raw, &parsed)) {
    [self warnOnceMethod:method key:key reason:@"non_integer_or_non_finite" value:raw action:@"ignored"];
    return NO;
  }
  *outValue = parsed;
  return YES;
}

- (BOOL)readOptionalIntegerFrom:(NSDictionary *)options method:(NSString *)method
                            key:(NSString *)key defaultValue:(NSInteger)defaultValue
                            min:(NSInteger)minValue max:(NSInteger)maxValue
                            out:(NSInteger *)outValue
{
  if (![self containsRawKey:options key:key]) { *outValue = defaultValue; return YES; }
  id raw = [options objectForKey:key];
  NSInteger parsed = 0;
  if (!ParseStrictIntegerFromAny(raw, &parsed)) {
    [self warnOnceMethod:method key:key reason:@"non_integer_or_non_finite" value:raw action:@"fallback_default"];
    *outValue = defaultValue;
    return YES;
  }
  NSInteger clamped = MAX(minValue, MIN(maxValue, parsed));
  if (clamped != parsed) {
    [self warnOnceMethod:method key:key reason:@"out_of_range" value:raw action:@"clamped"];
  }
  *outValue = clamped;
  return YES;
}

- (BOOL)readOptionalDoubleFrom:(NSDictionary *)options method:(NSString *)method
                           key:(NSString *)key defaultValue:(double)defaultValue
                           min:(double)minValue max:(double)maxValue
                           out:(double *)outValue
{
  if (![self containsRawKey:options key:key]) { *outValue = defaultValue; return YES; }
  id raw = [options objectForKey:key];
  double parsed = 0.0;
  if (!ParseFiniteDoubleFromAny(raw, &parsed)) {
    [self warnOnceMethod:method key:key reason:@"non_numeric_or_non_finite" value:raw action:@"fallback_default"];
    *outValue = defaultValue;
    return YES;
  }
  double clamped = ClampDouble(parsed, minValue, maxValue);
  if (clamped != parsed) {
    [self warnOnceMethod:method key:key reason:@"out_of_range" value:raw action:@"clamped"];
  }
  *outValue = clamped;
  return YES;
}

// ---------------------------------------------------------------------------
#pragma mark - Voice management
// ---------------------------------------------------------------------------

- (void)clearVoices
{
  for (int i = 0; i < ABSOLUTE_MAX_VOICES; i++) {
    voices[i].isActive      = false;
    voices[i].phase         = 0.0;
    voices[i].phaseIncrement= 0.0;
    voices[i].totalSamples  = 0;
    voices[i].samplesPlayed = 0;
    voices[i].attackSamples = 0;
    voices[i].releaseSamples= 0;
    voices[i].volume        = VOLUME_DEFAULT;
    voices[i].currentEnvelope = 0.0;
    voices[i].waveType      = 0;
    voices[i].pan           = 0.0;
    voices[i].panDelta      = 0.0;
    voices[i].leftGain      = M_SQRT1_2;
    voices[i].rightGain     = M_SQRT1_2;
    voices[i].lfoDepth      = 0.0;
    voices[i].noisePhase    = 0.0;
    voices[i].noisePhaseIncrement = 1.0;
    voices[i].noisePhaseIncrDelta = 0.0;
    voices[i].heldNoise     = 0.0;
    voices[i].filterState   = 0.0;
    voices[i].filterAlpha   = 0.0;
    voices[i].pulseWidth    = 0.5;
    voices[i].pulseWidthDelta = 0.0;
    voices[i].bitDepth      = 0;
    voices[i].wavetableSize = 0;
    voices[i].decaySamples  = 0;
    voices[i].sustain       = 1.0;
    voices[i].svfSweeping   = false;
    voices[i].fmActive      = false;
    voices[i].seqStepCount  = 0;
    tails[i].isActive       = false;
  }
  synth_stats_stopped(&stats);
  // Clear lookahead buffer to prevent stale audio leaking after stop
  if (lookaheadL && lookaheadR) {
    memset(lookaheadL, 0, lookaheadSize * sizeof(double));
    memset(lookaheadR, 0, lookaheadSize * sizeof(double));
  }
  smoothedGain = 1.0;
  smoothedMasterGain = 1.0;
  masterGainTarget = 1.0;
  masterGainStep = 0.0;
  masterGainFramesRemaining = 0;
  limiterAttackTarget = 1.0;
  limiterAttackStep = 0.0;
  lookaheadPeakHead = 0;
  lookaheadPeakCount = 0;
  limiterSampleIndex = 0;
}

- (void)updatePanGainsForVoice:(SynthVoice *)voice
{
  double angle = (voice->pan + 1.0) * M_PI * 0.25;
  voice->leftGain = cos(angle);
  voice->rightGain = sin(angle);
}

- (TailVoice *)findTailSlot
{
  TailVoice *fallback = &tails[0];
  for (int i = 0; i < engineMaxVoices; i++) {
    TailVoice *tail = &tails[i];
    if (!tail->isActive) return tail;
    if (tail->samplesRemaining < fallback->samplesRemaining) fallback = tail;
  }
  return fallback;
}

- (void)snapshotTailFromVoice:(SynthVoice *)voice fadeSamples:(int)fadeSamples
{
  TailVoice *tail = [self findTailSlot];
  synth_stats_tail_start(&stats, tail->isActive);
  tail->isActive = true;
  tail->spatial = voice->spatial;
  if (tail->spatial) {
    tail->spatialState = voice->spatialState;
    tail->spatialDrain = tail->spatialState.taps - 1;
  }
  tail->phase = voice->phase;
  tail->phaseIncrement = ClampDouble(voice->phaseIncrement, -0.45, 0.45);
  tail->volume = voice->volume * ClampDouble(voice->currentEnvelope, 0.0, 1.0);
  tail->waveType = voice->waveType;
  tail->leftGain = voice->leftGain;
  tail->rightGain = voice->rightGain;
  tail->noiseState = voice->noiseState;
  tail->noisePhase = voice->noisePhase;
  tail->noisePhaseIncrement = voice->noisePhaseIncrement;
  tail->heldNoise = voice->heldNoise;
  tail->filterState = voice->filterState;
  tail->filterAlpha = voice->filterAlpha;
  tail->pulseWidth = voice->pulseWidth;
  tail->bitDepth = voice->bitDepth;
  tail->wavetableSize = voice->wavetableSize;
  if (tail->wavetableSize > 0) {
    memcpy(tail->wavetable, voice->wavetable,
           tail->wavetableSize * sizeof(double));
  }
  tail->svf = voice->svf;
  tail->fmActive = voice->fmActive;
  tail->fmRatio = voice->fmRatio;
  tail->fmModPhase = voice->fmModPhase;
  tail->fmDepth = voice->fmDepth;
  tail->totalSamples = MAX(1, fadeSamples);
  tail->samplesRemaining = tail->totalSamples;
}

- (void)fadeOutAllVoices
{
  for (int i = 0; i < engineMaxVoices; i++) {
    SynthVoice *voice = &voices[i];
    if (!voice->isActive) continue;
    [self snapshotTailFromVoice:voice fadeSamples:stopFadeSamples];
    synth_stats_voice_end(&stats, voice->spatial);
    voice->isActive = false;
  }
}

- (int)findVoiceSlot
{
  int fallbackIndex = 0;
  int minRemaining  = INT_MAX;
  for (int i = 0; i < engineMaxVoices; i++) {
    if (!voices[i].isActive) return i;
    int remaining = MAX(0, voices[i].totalSamples - voices[i].samplesPlayed);
    if (remaining < minRemaining) {
      minRemaining  = remaining;
      fallbackIndex = i;
    }
  }
  return fallbackIndex;
}

- (void)startVoiceFromCommand:(const SynthCommand *)cmd
{
  int slot    = [self findVoiceSlot];
  SynthVoice *v = &voices[slot];
  synth_stats_voice_start(&stats, v->isActive, v->spatial, cmd->spatial);
  if (v->isActive) [self snapshotTailFromVoice:v fadeSamples:stealFadeSamples];

  v->isActive       = true;
  v->spatial = cmd->spatial;
  if (v->spatial) {
    synth_spatial_init(&v->spatialState, cmd->spatialX, cmd->spatialY, engineSampleRate);
    v->spatialDrain = v->spatialState.taps - 1;
  }
  v->phase          = 0.0;
  v->totalSamples   = MAX(1, cmd->durationSamples);
  v->samplesPlayed  = 0;
  v->attackSamples  = MAX(0, cmd->attackSamples);
  v->releaseSamples = MAX(0, cmd->releaseSamples);
  v->currentEnvelope= 0.0;
  v->waveType       = ClampInt(cmd->waveType, 0, 5);
  v->volume         = ClampDouble(cmd->volume, VOLUME_MIN, VOLUME_MAX)
                    * VoiceGainForEvent(mixingProfile, v->waveType,
                                        cmd->eventVoiceCount);
  v->pan            = ClampDouble(cmd->pan, -1.0, 1.0);
  [self updatePanGainsForVoice:v];

  // Sequencer
  int seqCount = cmd->seqStepCount;
  if (seqCount > 0) {
    v->seqStepCount     = MIN(seqCount, SEQ_MAX_STEPS);
    v->seqCurrentStep   = 0;
    v->seqSamplesInStep = 0;
    int stepDurMs       = cmd->seqStepDurationMs;
    v->seqStepSamples   = MAX(1, MsToSamples(stepDurMs > 0 ? stepDurMs : 100, engineSampleRate));
    for (int i = 0; i < v->seqStepCount; i++) {
      v->seqFreqs[i] = cmd->seqFreqs[i];
    }
    v->phaseIncrement = v->seqFreqs[0] / engineSampleRate;
  } else {
    v->seqStepCount   = 0;
    v->phaseIncrement = cmd->frequency / engineSampleRate;
  }

  // Pitch sweep
  double freqEnd = cmd->frequencyEnd;
  if (freqEnd >= MIN_FREQUENCY_HZ && v->totalSamples > 1) {
    double phaseIncrEnd = freqEnd / engineSampleRate;
    v->phaseIncrDelta = (phaseIncrEnd - v->phaseIncrement) / (v->totalSamples - 1);
  } else {
    v->phaseIncrDelta = 0.0;
  }

  // Pan sweep
  double panEnd = ClampDouble(cmd->panEnd, -1.0, 1.0);
  if (panEnd != v->pan && v->totalSamples > 1) {
    v->panDelta = (panEnd - v->pan) / (v->totalSamples - 1);
  } else {
    v->panDelta = 0.0;
  }

  // LFO
  v->lfoDepth          = ClampDouble(cmd->lfoDepth, 0.0, 20000.0);
  v->lfoPhase          = 0.0;
  double lfoFreq       = cmd->lfoFreq;
  v->lfoPhaseIncrement = lfoFreq > 0.0 ? lfoFreq / engineSampleRate : 0.0;

  // Variable-rate sample-and-hold noise. A zero rate preserves 1.0 behavior:
  // a fresh white-noise value for every output sample.
  double noiseRate = cmd->noiseRate > 0.0
                   ? ClampDouble(cmd->noiseRate, 1.0, engineSampleRate * 0.45)
                   : engineSampleRate;
  v->noisePhase = 0.0;
  v->noisePhaseIncrement = noiseRate / engineSampleRate;
  if (cmd->noiseRateEnd >= 1.0 && v->totalSamples > 1) {
    double noiseEnd = ClampDouble(cmd->noiseRateEnd, 1.0, engineSampleRate * 0.45)
                    / engineSampleRate;
    v->noisePhaseIncrDelta = (noiseEnd - v->noisePhaseIncrement)
                           / (v->totalSamples - 1);
  } else {
    v->noisePhaseIncrDelta = 0.0;
  }

  double cutoff = ClampDouble(cmd->lowPassHz, 0.0, engineSampleRate * 0.45);
  v->filterState = 0.0;
  v->filterAlpha = cutoff > 0.0
                 ? 1.0 - exp(-2.0 * M_PI * cutoff / engineSampleRate)
                 : 0.0;
  v->pulseWidth = ClampDouble(cmd->pulseWidth, 0.05, 0.95);
  if (cmd->pulseWidthEnd >= 0.05 && v->totalSamples > 1) {
    v->pulseWidthDelta = (ClampDouble(cmd->pulseWidthEnd, 0.05, 0.95)
                         - v->pulseWidth) / (v->totalSamples - 1);
  } else {
    v->pulseWidthDelta = 0.0;
  }
  v->bitDepth = cmd->bitDepth == 0 ? 0 : ClampInt(cmd->bitDepth, 2, 16);
  v->wavetableSize = ClampInt(cmd->wavetableSize, 0, WAVETABLE_MAX_SAMPLES);
  if (v->wavetableSize > 0) {
    memcpy(v->wavetable, cmd->wavetable, v->wavetableSize * sizeof(double));
  }

  // Decay and sustain
  v->decaySamples = MAX(0, cmd->decaySamples);
  v->sustain      = ClampDouble(cmd->sustain, 0.0, 1.0);

  // State-variable filter
  int filterType = ClampInt(cmd->filterType, SYNTH_FILTER_NONE, SYNTH_FILTER_BANDPASS);
  synth_filter_init(&v->svf, filterType);
  v->svfHz       = cmd->filterHz;
  v->svfHzEnd    = cmd->filterHzEnd;
  v->svfQ        = cmd->filterQ;
  v->svfSweeping = filterType != SYNTH_FILTER_NONE
                 && cmd->filterHzEnd >= SYNTH_FILTER_MIN_HZ && v->totalSamples > 1;

  // Phase modulation, sine carrier only
  v->fmActive   = (cmd->fmDepth > 0.0 || cmd->hasFmDepthEnd) && v->waveType == 0;
  v->fmRatio    = cmd->fmRatio;
  v->fmModPhase = 0.0;
  v->fmDepth    = ClampDouble(cmd->fmDepth, 0.0, 10.0);
  if (cmd->hasFmDepthEnd && v->totalSamples > 1) {
    v->fmDepthDelta = (ClampDouble(cmd->fmDepthEnd, 0.0, 10.0) - v->fmDepth)
                    / (v->totalSamples - 1);
  } else {
    v->fmDepthDelta = 0.0;
  }

  // Noise seed
  v->noiseState = (uint64_t)(mach_absolute_time()) ^ ((uint64_t)(slot + 1) * 0x9e3779b97f4a7c15ULL);
  v->heldNoise = LcgNoise(&v->noiseState);
}

// ---------------------------------------------------------------------------
#pragma mark - Command queue
// ---------------------------------------------------------------------------

- (BOOL)enqueueCommand:(SynthCommand)cmd
{
  // playPattern and direct API calls are independent producers. Serialize them
  // without ever taking a lock on the real-time audio consumer.
  os_unfair_lock_lock(&commandProducerLock);
  uint32_t write = atomic_load_explicit(&commandWriteIndex, memory_order_relaxed);
  uint32_t read  = atomic_load_explicit(&commandReadIndex,  memory_order_acquire);
  uint32_t next  = (write + 1) % COMMAND_QUEUE_CAPACITY;

  if (next == read) {
    // SPSC invariant: the audio thread alone advances commandReadIndex.
    if (cmd.type == CMD_STOP_ALL) {
      atomic_store_explicit(&emergencyStopRequested, true, memory_order_release);
    } else if (cmd.type == CMD_SHUTDOWN) {
      atomic_store_explicit(&emergencyShutdownRequested, true, memory_order_release);
    }
    os_unfair_lock_unlock(&commandProducerLock);
    return YES;
  }

  commandQueue[write] = cmd;
  atomic_store_explicit(&commandWriteIndex, next, memory_order_release);
  os_unfair_lock_unlock(&commandProducerLock);
  return NO;
}

// Convenience for CMD_STOP_ALL / CMD_SHUTDOWN
- (BOOL)enqueueSimpleCommand:(int)type
{
  SynthCommand cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = type;
  return [self enqueueCommand:cmd];
}

// ---------------------------------------------------------------------------
#pragma mark - Public APIs
// ---------------------------------------------------------------------------

- (NSDictionary *)getStats:(id)args
{
  @synchronized(self) {
#pragma unused(args)
  double v[SYNTH_STATS_COUNT]; synth_stats_read(&stats,v);
  NSMutableDictionary *r=[NSMutableDictionary dictionary];
  NSArray *keys=@[@"activeVoices",@"peakActiveVoices",@"tailVoices",@"spatialVoices",@"voiceStealCount",@"callbackFrames",@"renderLoad",@"peakRenderLoad"];
  for(int i=0;i<8;i++) r[keys[i]]=isnan(v[i]) ? (id)[NSNull null] : @(v[i]);
  r[@"outputPeakDbFS"]=isnan(v[ST_PEAK_L]) ? (id)[NSNull null] : @{@"left":@(v[ST_PEAK_L]),@"right":@(v[ST_PEAK_R])};
  r[@"outputRmsDbFS"]=isnan(v[ST_RMS_L]) ? (id)[NSNull null] : @{@"left":@(v[ST_RMS_L]),@"right":@(v[ST_RMS_R])};
  r[@"limiterReductionDb"]=isnan(v[ST_REDUCTION]) ? (id)[NSNull null] : @(v[ST_REDUCTION]);
  os_unfair_lock_lock(&commandProducerLock);
  uint32_t rd=atomic_load_explicit(&commandReadIndex,memory_order_acquire);
  uint32_t wr=atomic_load_explicit(&commandWriteIndex,memory_order_acquire);
  r[@"pendingCommands"]=@((wr+COMMAND_QUEUE_CAPACITY-rd)%COMMAND_QUEUE_CAPACITY);
  os_unfair_lock_unlock(&commandProducerLock);
  r[@"maxVoices"]=@(engineMaxVoices); r[@"sampleRate"]=@(engineSampleRate);
  r[@"underrunCount"]=[NSNull null]; r[@"bufferFrames"]=[NSNull null]; r[@"estimatedOutputLatencyMs"]=[NSNull null];
  uint64_t validation=synth_stats_load(&statsRejected[0]), stopped=synth_stats_load(&statsRejected[1]), full=synth_stats_load(&statsRejected[2]);
  r[@"rejectedRequestsByReason"]=@{@"validation":@(validation),@"engineStopped":@(stopped),@"queueFull":@(full)};
  r[@"rejectedRequestCount"]=@(validation+stopped+full);
  r[@"droppedPatternStepCount"]=@(synth_stats_load(&droppedPatternSteps));
  return r;
  }
}
- (NSNumber *)setStatsEnabled:(id)args
{
  @synchronized(self) {
  id enabled=[args isKindOfClass:[NSArray class]] && [args count]==1 ? args[0] : args;
  if(![enabled isKindOfClass:[NSNumber class]] || CFGetTypeID((__bridge CFTypeRef)enabled)!=CFBooleanGetTypeID()) {
    __atomic_fetch_add(&statsRejected[0],1,__ATOMIC_RELAXED); return @NO;
  }
  statsEnabled=[enabled boolValue]; synth_stats_enable(&stats,statsEnabled); return @YES;
  }
}
- (void)resetStats:(id)args
{
  @synchronized(self) {
#pragma unused(args)
  for(int i=0;i<3;i++) synth_stats_store(&statsRejected[i],0);
  synth_stats_store(&droppedPatternSteps,0); synth_stats_reset(&stats);
  }
}

- (NSDictionary *)getDefaults:(id)args
{
  @synchronized(self) {
#pragma unused(args)
  return [TiSynthengineContract defaultsWithMaxFrequency:[self maxFrequencyHz]];
  }
}

- (NSNumber *)startEngine:(id)args
{
  @synchronized(self) {
  id raw=[args isKindOfClass:[NSArray class]] && [args count]==1 ? args[0] : args;
  if(raw==nil) raw=@{};
  if(![raw isKindOfClass:[NSDictionary class]]) { __atomic_fetch_add(&statsRejected[0],1,__ATOMIC_RELAXED); return @NO; }
  NSDictionary *options=raw;
  if (![self acceptValidation:[TiSynthengineContract validateStartEngine:options]
                       method:@"startEngine"]) return @NO;
  if (audioEngine != nil) return @YES;
  SynthStats previousStats=stats;
  int previousMaxVoices=engineMaxVoices;
  double previousSampleRate=engineSampleRate;

  NSInteger maxVoices = 0;
  [self readOptionalIntegerFrom:options method:@"startEngine" key:@"maxVoices"
                   defaultValue:8 min:1 max:ABSOLUTE_MAX_VOICES out:&maxVoices];
  engineMaxVoices = (int)maxVoices;

  mixingProfile = MIXING_PROFILE_CONSERVATIVE;
  id mixingProfileValue = [options objectForKey:@"mixingProfile"];
  if (mixingProfileValue != nil) {
    if (![mixingProfileValue isKindOfClass:[NSString class]]) {
      [self warnOnceMethod:@"startEngine" key:@"mixingProfile" reason:@"not_string"
                     value:mixingProfileValue action:@"engine_not_started"];
      return @NO;
    }
    NSString *profile = [[(NSString *)mixingProfileValue
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]
        lowercaseString];
    if ([profile isEqualToString:@"conservative"]) {
      mixingProfile = MIXING_PROFILE_CONSERVATIVE;
    } else if ([profile isEqualToString:@"balanced"]) {
      mixingProfile = MIXING_PROFILE_BALANCED;
    } else if ([profile isEqualToString:@"speakersafe"]) {
      mixingProfile = MIXING_PROFILE_SPEAKER_SAFE;
    } else if (![profile isEqualToString:@"raw"]) {
      [self warnOnceMethod:@"startEngine" key:@"mixingProfile"
                     reason:@"unsupported_value" value:mixingProfileValue
                     action:@"engine_not_started"];
      return @NO;
    }
  }

  audioSessionCategory = AVAudioSessionCategoryPlayback;
  id audioSessionValue = [options objectForKey:@"audioSession"];
  if ([audioSessionValue isKindOfClass:[NSString class]]
      && [[(NSString *)audioSessionValue stringByTrimmingCharactersInSet:
           [NSCharacterSet whitespaceAndNewlineCharacterSet]]
          caseInsensitiveCompare:@"ambient"] == NSOrderedSame) {
    audioSessionCategory = AVAudioSessionCategoryAmbient;
  }

  NSError *sessionError = nil;
  AVAudioSession *session = [AVAudioSession sharedInstance];
  [session setCategory:audioSessionCategory error:&sessionError];
  [session setActive:YES error:&sessionError];

  if(sessionError != nil) {
    engineMaxVoices=previousMaxVoices;
    __atomic_fetch_add(&statsRejected[1],1,__ATOMIC_RELAXED); return @NO;
  }
  double sessionSampleRate = [session sampleRate];
  engineSampleRate = (isfinite(sessionSampleRate) && sessionSampleRate > 0.0)
                   ? sessionSampleRate : DEFAULT_SAMPLE_RATE;

  stopFadeSamples  = MAX(1, MsToSamples(STOP_ALL_FADE_MS, engineSampleRate));
  stealFadeSamples = MAX(1, MsToSamples(VOICE_STEAL_FADE_MS, engineSampleRate));
  atomic_store_explicit(&masterVolume, 1.0, memory_order_relaxed);
  atomic_store_explicit(&emergencyStopRequested, false, memory_order_relaxed);
  atomic_store_explicit(&emergencyShutdownRequested, false, memory_order_relaxed);
  smoothedGain        = 1.0;
  smoothedMasterGain  = 1.0;
  masterGainTarget    = 1.0;
  masterGainStep      = 0.0;
  masterGainFramesRemaining = 0;
  limiterAttackTarget = 1.0;
  limiterAttackStep   = 0.0;
  gainReleaseCoeff = 1.0 - exp(-1.0 / (0.100 * engineSampleRate));
  lookaheadSize       = MAX(1, (int)round(0.010 * engineSampleRate));
  lookaheadIdx        = 0;
  lookaheadL          = (double *)calloc(lookaheadSize, sizeof(double));
  lookaheadR          = (double *)calloc(lookaheadSize, sizeof(double));
  lookaheadPeaks      = (double *)calloc(lookaheadSize + 1, sizeof(double));
  lookaheadPeakIndices= (int64_t *)calloc(lookaheadSize + 1, sizeof(int64_t));

  [self clearVoices];
  atomic_store_explicit(&commandWriteIndex, 0, memory_order_relaxed);
  atomic_store_explicit(&commandReadIndex,  0, memory_order_relaxed);

  synth_stats_init(&stats);
  synth_stats_enable(&stats,statsEnabled);
  audioEngine = [[AVAudioEngine alloc] init];
  AVAudioFormat *format = [[AVAudioFormat alloc]
      initStandardFormatWithSampleRate:engineSampleRate channels:2];

  if (@available(iOS 13.0, *)) {
    __unsafe_unretained TiSynthengineModule *module = self;

    AVAudioSourceNodeRenderBlock renderBlock =
        ^OSStatus(BOOL *isSilence, const AudioTimeStamp *timestamp,
                  AVAudioFrameCount frameCount, AudioBufferList *outputData)
    {
#pragma unused(isSilence, timestamp)
      BOOL measure = synth_stats_begin(&module->stats);
      uint64_t statsBegin = measure ? mach_absolute_time() : 0;
      // Process all pending commands
      if (atomic_exchange_explicit(&module->emergencyShutdownRequested, false,
                                   memory_order_acq_rel)) {
        [module clearVoices];
      }
      if (atomic_exchange_explicit(&module->emergencyStopRequested, false,
                                   memory_order_acq_rel)) {
        [module fadeOutAllVoices];
      }
      uint32_t rd = atomic_load_explicit(&module->commandReadIndex, memory_order_acquire);
      uint32_t wr = atomic_load_explicit(&module->commandWriteIndex, memory_order_acquire);

      while (rd != wr) {
        SynthCommand cmd = module->commandQueue[rd];
        if (cmd.type == CMD_NOTE_ON) {
          [module startVoiceFromCommand:&cmd];
        } else if (cmd.type == CMD_STOP_ALL) {
          [module fadeOutAllVoices];
        } else if (cmd.type == CMD_SHUTDOWN) {
          [module clearVoices];
        }
        rd = (rd + 1) % COMMAND_QUEUE_CAPACITY;
      }
      atomic_store_explicit(&module->commandReadIndex, rd, memory_order_release);

      if (outputData->mNumberBuffers == 0) return noErr;

      float *outL = (float *)outputData->mBuffers[0].mData;
      float *outR = outputData->mNumberBuffers > 1
                  ? (float *)outputData->mBuffers[1].mData : NULL;
      BOOL isInterleaved = (outR == NULL && outputData->mBuffers[0].mNumberChannels >= 2);

      double mv = atomic_load_explicit(&module->masterVolume, memory_order_relaxed);

      for (AVAudioFrameCount frame = 0; frame < frameCount; frame++) {
        if (mv != module->masterGainTarget) {
          module->masterGainTarget = mv;
          module->masterGainFramesRemaining = module->lookaheadSize;
          module->masterGainStep = (module->masterGainTarget
                                    - module->smoothedMasterGain)
                                 / module->masterGainFramesRemaining;
        }
        if (module->masterGainFramesRemaining > 0) {
          module->smoothedMasterGain += module->masterGainStep;
          module->masterGainFramesRemaining--;
          if (module->masterGainFramesRemaining == 0) {
            module->smoothedMasterGain = module->masterGainTarget;
            module->masterGainStep = 0.0;
          }
        }

        double mixL = 0.0;
        double mixR = 0.0;

        if (module->stats.active > 0) for (int i = 0; i < module->engineMaxVoices; i++) {
          SynthVoice *v = &module->voices[i];
          if (!v->isActive) continue;
          if (v->spatial && v->samplesPlayed >= v->totalSamples) {
            float l, r;
            synth_spatial_process(&v->spatialState, 0, &l, &r);
            mixL += l; mixR += r;
            if (--v->spatialDrain <= 0) { v->isActive = false; synth_stats_voice_end(&module->stats,true); }
            continue;
          }

          // --- Sequencer step advance ---
          if (v->seqStepCount > 0) {
            v->seqSamplesInStep++;
            if (v->seqSamplesInStep >= v->seqStepSamples) {
              v->seqCurrentStep = (v->seqCurrentStep + 1) % v->seqStepCount;
              v->phaseIncrement = v->seqFreqs[v->seqCurrentStep] / module->engineSampleRate;
              v->seqSamplesInStep = 0;
            }
          }

          // --- LFO vibrato ---
          double effectivePhaseIncr = v->phaseIncrement;
          if (v->lfoDepth > 0.0) {
            double lfoSample = sin(v->lfoPhase * 2.0 * M_PI);
            effectivePhaseIncr += lfoSample * v->lfoDepth / module->engineSampleRate;
            v->lfoPhase += v->lfoPhaseIncrement;
            if (v->lfoPhase >= 1.0) v->lfoPhase -= 1.0;
          }

          // --- Waveform generation ---
          effectivePhaseIncr = ClampDouble(effectivePhaseIncr, -0.45, 0.45);
          double wf = 0.0;
          if (v->waveType == 4) {
            v->noisePhase += ClampDouble(v->noisePhaseIncrement, 1.0e-9, 1.0);
            if (v->noisePhase >= 1.0) {
              v->noisePhase -= floor(v->noisePhase);
              v->heldNoise = LcgNoise(&v->noiseState);
            }
            wf = v->heldNoise;
          } else if (v->waveType == 5) {
            wf = RenderWavetable(v->phase, v->wavetable, v->wavetableSize);
          } else if (v->fmActive) {
            wf = synth_fm_sine(v->phase - floor(v->phase), v->fmModPhase, v->fmDepth);
          } else {
            wf = RenderPeriodicWave(v->waveType, v->phase, effectivePhaseIncr,
                                    v->pulseWidth);
          }
          wf = QuantizeWave(wf, v->bitDepth);
          if (v->filterAlpha > 0.0) {
            v->filterState += v->filterAlpha * (wf - v->filterState);
            wf = v->filterState;
          }
          if (v->svf.type != SYNTH_FILTER_NONE) {
            if (synth_filter_update_due(v->svfSweeping, v->samplesPlayed)) {
              double cutoffHz = v->svfSweeping
                  ? synth_geometric_at(v->svfHz, v->svfHzEnd,
                                       synth_sweep_progress(v->samplesPlayed, v->totalSamples))
                  : v->svfHz;
              synth_filter_set(&v->svf, cutoffHz, v->svfQ, module->engineSampleRate);
            }
            wf = synth_filter_process(&v->svf, wf);
          }

          // --- Envelope (smoothstep attack, decay to sustain, release) ---
          double envelope = synth_envelope(v->samplesPlayed, v->totalSamples,
                                           v->attackSamples, v->decaySamples,
                                           v->sustain, v->releaseSamples);
          v->currentEnvelope = envelope;

          // --- Voice mix (optional bus protection is applied below) ---
          double sample = wf * envelope * v->volume;
          if (v->spatial) {
            float spatialL, spatialR;
            synth_spatial_process(&v->spatialState, (float)sample, &spatialL, &spatialR);
            mixL += spatialL; mixR += spatialR;
          } else {
            mixL += sample * v->leftGain;
            mixR += sample * v->rightGain;
          }

          // --- Advance phase ---
          v->phase += effectivePhaseIncr;
          v->phase -= floor(v->phase);
          if (v->fmActive) {
            v->fmModPhase += effectivePhaseIncr * v->fmRatio;
            v->fmModPhase -= floor(v->fmModPhase);
            v->fmDepth = ClampDouble(v->fmDepth + v->fmDepthDelta, 0.0, 10.0);
          }

          // --- Pitch sweep ---
          if (v->phaseIncrDelta != 0.0) {
            v->phaseIncrement += v->phaseIncrDelta;
          }
          v->noisePhaseIncrement += v->noisePhaseIncrDelta;
          v->pulseWidth = ClampDouble(v->pulseWidth + v->pulseWidthDelta, 0.05, 0.95);

          // --- Pan sweep ---
          if (v->panDelta != 0.0) {
            v->pan = ClampDouble(v->pan + v->panDelta, -1.0, 1.0);
            [module updatePanGainsForVoice:v];
          }

          // --- Advance sample counter ---
          v->samplesPlayed++;
          if (v->samplesPlayed >= v->totalSamples) {
            if (v->spatial) v->currentEnvelope = 0;
            else { v->isActive = false; synth_stats_voice_end(&module->stats,false); }
          }
        }

        if (module->stats.tails > 0) for (int i = 0; i < module->engineMaxVoices; i++) {
          TailVoice *tail = &module->tails[i];
          if (!tail->isActive) continue;
          if (tail->spatial && tail->samplesRemaining <= 0) {
            float l, r;
            synth_spatial_process(&tail->spatialState, 0, &l, &r);
            mixL += l; mixR += r;
            if (--tail->spatialDrain <= 0) { tail->isActive = false; synth_stats_tail_end(&module->stats); }
            continue;
          }
          double wf = 0.0;
          if (tail->waveType == 4) {
            tail->noisePhase += tail->noisePhaseIncrement;
            if (tail->noisePhase >= 1.0) {
              tail->noisePhase -= floor(tail->noisePhase);
              tail->heldNoise = LcgNoise(&tail->noiseState);
            }
            wf = tail->heldNoise;
          } else if (tail->waveType == 5) {
            wf = RenderWavetable(tail->phase, tail->wavetable, tail->wavetableSize);
          } else if (tail->fmActive) {
            wf = synth_fm_sine(tail->phase - floor(tail->phase), tail->fmModPhase, tail->fmDepth);
          } else {
            wf = RenderPeriodicWave(tail->waveType, tail->phase,
                                    tail->phaseIncrement, tail->pulseWidth);
          }
          wf = QuantizeWave(wf, tail->bitDepth);
          if (tail->filterAlpha > 0.0) {
            tail->filterState += tail->filterAlpha * (wf - tail->filterState);
            wf = tail->filterState;
          }
          wf = synth_filter_process(&tail->svf, wf);
          int denominator = MAX(1, tail->totalSamples - 1);
          double x = ClampDouble((double)(tail->samplesRemaining - 1)
                                 / (double)denominator, 0.0, 1.0);
          double tailEnvelope = SmoothStep01(x);
          double sample = wf * tail->volume * tailEnvelope;
          if (tail->spatial) {
            float spatialL, spatialR;
            synth_spatial_process(&tail->spatialState, (float)sample, &spatialL, &spatialR);
            mixL += spatialL; mixR += spatialR;
          } else {
            mixL += sample * tail->leftGain;
            mixR += sample * tail->rightGain;
          }
          tail->phase += tail->phaseIncrement;
          tail->phase -= floor(tail->phase);
          if (tail->fmActive) {
            tail->fmModPhase += tail->phaseIncrement * tail->fmRatio;
            tail->fmModPhase -= floor(tail->fmModPhase);
          }
          tail->samplesRemaining--;
          if (tail->samplesRemaining <= 0 && !tail->spatial) { tail->isActive = false; synth_stats_tail_end(&module->stats); }
        }

        // --- Stereo-linked lookahead peak limiter ---
        int laIdx = module->lookaheadIdx;
        double delayedL = module->lookaheadL[laIdx];
        double delayedR = module->lookaheadR[laIdx];
        double scaledL = mixL * module->smoothedMasterGain;
        double scaledR = mixR * module->smoothedMasterGain;
        module->lookaheadL[laIdx] = scaledL;
        module->lookaheadR[laIdx] = scaledR;
        module->lookaheadIdx = (laIdx + 1) % module->lookaheadSize;

        // Maintain the maximum of the complete future window in amortized O(1).
        int peakCapacity = module->lookaheadSize + 1;
        int64_t oldestIndex = module->limiterSampleIndex - module->lookaheadSize;
        while (module->lookaheadPeakCount > 0
               && module->lookaheadPeakIndices[module->lookaheadPeakHead]
                    < oldestIndex) {
          module->lookaheadPeakHead = (module->lookaheadPeakHead + 1) % peakCapacity;
          module->lookaheadPeakCount--;
        }

        double currentPeak = fmax(fabs(scaledL), fabs(scaledR));
        while (module->lookaheadPeakCount > 0) {
          int last = (module->lookaheadPeakHead
                      + module->lookaheadPeakCount - 1) % peakCapacity;
          if (module->lookaheadPeaks[last] > currentPeak) break;
          module->lookaheadPeakCount--;
        }
        int insert = (module->lookaheadPeakHead
                      + module->lookaheadPeakCount) % peakCapacity;
        module->lookaheadPeaks[insert] = currentPeak;
        module->lookaheadPeakIndices[insert] = module->limiterSampleIndex;
        module->lookaheadPeakCount++;
        module->limiterSampleIndex++;

        // A shared window gain preserves waveform shape instead of flattening
        // individual peaks with a sample-by-sample safety gain.
        double windowPeak = module->lookaheadPeaks[module->lookaheadPeakHead];

        double outputCeiling = module->mixingProfile == MIXING_PROFILE_SPEAKER_SAFE
                             ? SPEAKER_SAFE_OUTPUT_CEILING : OUTPUT_CEILING;
        double targetGain = windowPeak > outputCeiling
                          ? outputCeiling / windowPeak : 1.0;

        // A new maximum enters at the far end of the lookahead buffer. Ramp
        // toward its required gain over that deadline instead of introducing
        // an instantaneous gain discontinuity. Do not slow an attack already
        // in flight because an earlier peak can have a nearer deadline.
        if (targetGain < module->smoothedGain) {
          module->limiterAttackTarget = fmin(
              module->limiterAttackTarget, targetGain);
          double requiredStep = (module->limiterAttackTarget
                                 - module->smoothedGain)
                              / module->lookaheadSize;
          module->limiterAttackStep = fmin(
              module->limiterAttackStep, requiredStep);
        }

        if (module->limiterAttackStep < 0.0
            && module->smoothedGain > module->limiterAttackTarget) {
          module->smoothedGain = fmax(
              module->limiterAttackTarget,
              module->smoothedGain + module->limiterAttackStep);
        } else {
          module->limiterAttackTarget = 1.0;
          module->limiterAttackStep = 0.0;
          module->smoothedGain = fmin(
              targetGain,
              module->smoothedGain
                  + (1.0 - module->smoothedGain) * module->gainReleaseCoeff);
        }

        // A fully drained lookahead window has no remaining waveform to protect.
        // Reset only dynamic bus gain so the next unrelated sound starts at its
        // intended level without truncating a release tail or creating a click.
        BOOL drainedWindow = currentPeak <= 1.0e-12
            && windowPeak <= 1.0e-12
            && fabs(delayedL) <= 1.0e-12
            && fabs(delayedR) <= 1.0e-12;
        if (drainedWindow) {
          module->smoothedGain = 1.0;
          module->limiterAttackTarget = 1.0;
          module->limiterAttackStep = 0.0;
          module->lookaheadPeakHead = 0;
          module->lookaheadPeakCount = 0;
        }
        double finalL = delayedL * module->smoothedGain;
        double finalR = delayedR * module->smoothedGain;

        float fL = (float)ClampDouble(finalL, -1.0, 1.0);
        float fR = (float)ClampDouble(finalR, -1.0, 1.0);
        if(measure) synth_stats_sample(&module->stats,fL,fR,module->smoothedGain,(int)module->engineSampleRate);

        if (outR != NULL) {
          outL[frame] = fL;
          outR[frame] = fR;
        } else if (isInterleaved) {
          outL[frame * 2]     = fL;
          outL[frame * 2 + 1] = fR;
        } else {
          outL[frame] = fL;
        }
      }

      double seconds = measure ? (mach_absolute_time()-statsBegin) * (double)module->statsTimebase.numer / module->statsTimebase.denom * 1e-9 : 0;
      synth_stats_end(&module->stats,(int)frameCount,(int)module->engineSampleRate,seconds);
      return noErr;
    };

    sourceNode = [[AVAudioSourceNode alloc] initWithRenderBlock:renderBlock];
    [audioEngine attachNode:sourceNode];
    [audioEngine connect:sourceNode to:audioEngine.mainMixerNode format:format];
  } else {
    DebugLog(@"[ERROR] AVAudioSourceNode requires iOS 13.0+");
    audioEngine = nil;
    sourceNode  = nil;
    return @NO;
  }

  NSError *engineError = nil;
  BOOL started=[audioEngine startAndReturnError:&engineError];
  if (!started || engineError != nil) {
    DebugLog(@"[ERROR] Unable to start audio engine: %@", engineError.localizedDescription);
    [self shutdown:nil];
    stats=previousStats; engineMaxVoices=previousMaxVoices; engineSampleRate=previousSampleRate;
    __atomic_fetch_add(&statsRejected[1],1,__ATOMIC_RELAXED);
    return @NO;
  }
  for(int i=0;i<3;i++) synth_stats_store(&statsRejected[i],0);
  synth_stats_store(&droppedPatternSteps,0);
  return @YES;
  }
}

- (NSNumber *)setVolume:(id)args
{
  @synchronized(self) {
  if (audioEngine == nil) { __atomic_fetch_add(&statsRejected[1],1,__ATOMIC_RELAXED); return @NO; }

  id raw=[args isKindOfClass:[NSArray class]] && [args count]==1 ? args[0] : args;
  if(![raw isKindOfClass:[NSDictionary class]]) { __atomic_fetch_add(&statsRejected[0],1,__ATOMIC_RELAXED); return @NO; }
  NSDictionary *options=raw;
  if (![self acceptValidation:[TiSynthengineContract validateSetVolume:options]
                       method:@"setVolume"]) return @NO;
  double volume = 1.0;
  [self readOptionalDoubleFrom:options method:@"setVolume" key:@"volume"
                  defaultValue:1.0 min:VOLUME_MIN max:VOLUME_MAX out:&volume];
  atomic_store_explicit(&masterVolume, volume, memory_order_relaxed);
  return @YES;
  }
}

- (NSNumber *)playTone:(id)args
{
  @synchronized(self) {
  if (audioEngine == nil || engineMaxVoices < 1) { __atomic_fetch_add(&statsRejected[1],1,__ATOMIC_RELAXED); return @NO; }

  id raw=[args isKindOfClass:[NSArray class]] && [args count]==1 ? args[0] : args;
  if(raw==nil) raw=@{};
  if(![raw isKindOfClass:[NSDictionary class]]) { __atomic_fetch_add(&statsRejected[0],1,__ATOMIC_RELAXED); return @NO; }
  NSDictionary *options=raw;
  double maxFreq = [self maxFrequencyHz];
  if (![self acceptValidation:[TiSynthengineContract validateTone:options maxFrequency:maxFreq]
                       method:@"playTone"]) return @NO;

  double frequency = 0.0;

  SynthCommand cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_NOTE_ON;

  if ([self containsRawKey:options key:@"note"]) {
    id noteObj = [options objectForKey:@"note"];
    frequency = ParseNoteToFrequency([noteObj isKindOfClass:[NSString class]]
                                     ? (NSString *)noteObj : [noteObj description]);
    if (frequency < MIN_FREQUENCY_HZ) {
      [self warnOnceMethod:@"playTone" key:@"note" reason:@"invalid_note"
                     value:noteObj action:@"ignored"];
      return @NO;
    }
    frequency = ClampDouble(frequency, MIN_FREQUENCY_HZ, maxFreq);
  } else {
    [self readOptionalDoubleFrom:options method:@"playTone" key:@"frequency"
                   defaultValue:DEFAULT_FREQUENCY_HZ
                            min:MIN_FREQUENCY_HZ max:maxFreq out:&frequency];
  }
  cmd.frequency = frequency;

  NSInteger durationMs = 0;
  [self readOptionalIntegerFrom:options method:@"playTone" key:@"duration"
                   defaultValue:DURATION_DEFAULT_MS
                            min:DURATION_MIN_MS max:DURATION_MAX_MS out:&durationMs];

  NSInteger attackMs = 0, releaseMs = 0;
  double volume = 0.0, pan = 0.0, panEnd = 0.0, lfoFreq = 0.0, lfoDepth = 0.0;
  double noiseRate = 0.0, noiseRateEnd = 0.0, lowPassHz = 0.0;
  double pulseWidth = 0.5, pulseWidthEnd = 0.0;
  NSInteger waveType = 0, bitDepth = 0;

  [self readOptionalIntegerFrom:options method:@"playTone" key:@"attack"
                   defaultValue:ATTACK_DEFAULT_MS min:ATTACK_MIN_MS max:ATTACK_MAX_MS out:&attackMs];
  [self readOptionalIntegerFrom:options method:@"playTone" key:@"release"
                   defaultValue:RELEASE_DEFAULT_MS min:RELEASE_MIN_MS max:RELEASE_MAX_MS out:&releaseMs];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"volume"
                  defaultValue:VOLUME_DEFAULT min:VOLUME_MIN max:VOLUME_MAX out:&volume];
  [self readOptionalIntegerFrom:options method:@"playTone" key:@"waveType"
                   defaultValue:0 min:0 max:5 out:&waveType];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"pan"
                  defaultValue:0.0 min:-1.0 max:1.0 out:&pan];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"panEnd"
                  defaultValue:pan min:-1.0 max:1.0 out:&panEnd];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"lfoFreq"
                  defaultValue:0.0 min:0.0 max:20000.0 out:&lfoFreq];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"lfoDepth"
                  defaultValue:0.0 min:0.0 max:20000.0 out:&lfoDepth];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"noiseRate"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&noiseRate];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"noiseRateEnd"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&noiseRateEnd];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"lowPassHz"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&lowPassHz];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"pulseWidth"
                  defaultValue:0.5 min:0.05 max:0.95 out:&pulseWidth];
  [self readOptionalDoubleFrom:options method:@"playTone" key:@"pulseWidthEnd"
                  defaultValue:0.0 min:0.0 max:0.95 out:&pulseWidthEnd];
  [self readOptionalIntegerFrom:options method:@"playTone" key:@"bitDepth"
                   defaultValue:0 min:0 max:16 out:&bitDepth];

  double frequencyEnd = 0.0;
  if ([self containsRawKey:options key:@"frequencyEnd"]) {
    double fe = 0.0;
    if (ParseFiniteDoubleFromAny([options objectForKey:@"frequencyEnd"], &fe)
        && fe >= MIN_FREQUENCY_HZ) {
      frequencyEnd = ClampDouble(fe, MIN_FREQUENCY_HZ, maxFreq);
    }
  }

  if (attackMs >= durationMs) {
    [self warnOnceMethod:@"playTone" key:@"attack" reason:@"attack_greater_or_equal_duration"
                   value:@(attackMs) action:@"trimmed"];
    attackMs = MAX(0, durationMs - 1);
  }
  NSInteger maxRelease = MAX(0, durationMs - attackMs);
  if (releaseMs > maxRelease) {
    [self warnOnceMethod:@"playTone" key:@"release" reason:@"release_exceeds_remaining_duration"
                   value:@(releaseMs) action:@"trimmed"];
    releaseMs = maxRelease;
  }

  cmd.durationSamples = MAX(1, MsToSamples((int)durationMs, engineSampleRate));
  cmd.attackSamples   = MAX(1, MsToSamples((int)attackMs,   engineSampleRate));
  cmd.releaseSamples  = MAX(1, MsToSamples((int)releaseMs,  engineSampleRate));
  cmd.volume          = volume;
  cmd.waveType        = (int)waveType;
  cmd.pan             = pan;
  cmd.panEnd          = panEnd;
  cmd.frequencyEnd    = frequencyEnd;
  cmd.lfoFreq         = lfoFreq;
  cmd.lfoDepth        = lfoDepth;
  cmd.noiseRate       = noiseRate;
  cmd.noiseRateEnd    = noiseRateEnd;
  cmd.lowPassHz       = lowPassHz;
  cmd.pulseWidth      = pulseWidth;
  cmd.pulseWidthEnd   = pulseWidthEnd;
  cmd.bitDepth        = (int)bitDepth;
  CopyWaveTableFromOptions(options, NULL, 0, cmd.wavetable, &cmd.wavetableSize);

  ExpressiveParams expressive = ReadExpressiveParams(options, ExpressiveDefaults());
  ApplyExpressiveParams(&cmd, &expressive, engineSampleRate);
  cmd.frequency = DetunedFrequency(cmd.frequency, expressive.detune, maxFreq);
  if (cmd.frequencyEnd > 0.0) {
    cmd.frequencyEnd = DetunedFrequency(cmd.frequencyEnd, expressive.detune, maxFreq);
  }

  int maxAttackSamples = MAX(0, cmd.durationSamples - 1);
  if (cmd.attackSamples > maxAttackSamples) cmd.attackSamples = maxAttackSamples;
  int maxReleaseSamples = MAX(0, cmd.durationSamples - cmd.attackSamples);
  if (cmd.releaseSamples > maxReleaseSamples) cmd.releaseSamples = maxReleaseSamples;

  BOOL dropped = [self enqueueCommand:cmd];
  if (dropped) {
    [self warnOnceMethod:@"playTone" key:@"queue" reason:@"overflow"
                   value:@"command_queue_full" action:@"drop_newest"];
  }
  if(dropped) __atomic_fetch_add(&statsRejected[2],1,__ATOMIC_RELAXED);
  return @(!dropped);
  }
}

- (NSNumber *)playChord:(id)args
{
  @synchronized(self) {
  if (audioEngine == nil || engineMaxVoices < 1) { __atomic_fetch_add(&statsRejected[1],1,__ATOMIC_RELAXED); return @NO; }

  id raw=[args isKindOfClass:[NSArray class]] && [args count]==1 ? args[0] : args;
  if(![raw isKindOfClass:[NSDictionary class]]) { __atomic_fetch_add(&statsRejected[0],1,__ATOMIC_RELAXED); return @NO; }
  NSDictionary *options=raw;
  double maxFreq = [self maxFrequencyHz];
  if (![self acceptValidation:[TiSynthengineContract validateChord:options maxFrequency:maxFreq]
                       method:@"playChord"]) return @NO;

  NSArray *freqArr = [self parseFreqArray:options method:@"playChord" maxFreq:maxFreq];
  if (!freqArr || freqArr.count == 0) return @NO;

  NSInteger durationMs = 0;
  [self readOptionalIntegerFrom:options method:@"playChord" key:@"duration"
                   defaultValue:DURATION_DEFAULT_MS
                            min:DURATION_MIN_MS max:DURATION_MAX_MS out:&durationMs];

  NSInteger attackMs = 0, releaseMs = 0, waveType = 0, bitDepth = 0;
  double volume = 0.0, panSpread = 0.0, noiseRate = 0.0;
  double lowPassHz = 0.0, pulseWidth = 0.5;
  [self readOptionalIntegerFrom:options method:@"playChord" key:@"attack"
                   defaultValue:ATTACK_DEFAULT_MS min:ATTACK_MIN_MS max:ATTACK_MAX_MS out:&attackMs];
  [self readOptionalIntegerFrom:options method:@"playChord" key:@"release"
                   defaultValue:RELEASE_DEFAULT_MS min:RELEASE_MIN_MS max:RELEASE_MAX_MS out:&releaseMs];
  [self readOptionalDoubleFrom:options method:@"playChord" key:@"volume"
                  defaultValue:VOLUME_DEFAULT min:VOLUME_MIN max:VOLUME_MAX out:&volume];
  [self readOptionalIntegerFrom:options method:@"playChord" key:@"waveType"
                   defaultValue:0 min:0 max:5 out:&waveType];
  [self readOptionalDoubleFrom:options method:@"playChord" key:@"panSpread"
                  defaultValue:0.0 min:0.0 max:1.0 out:&panSpread];
  [self readOptionalDoubleFrom:options method:@"playChord" key:@"noiseRate"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&noiseRate];
  [self readOptionalDoubleFrom:options method:@"playChord" key:@"lowPassHz"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&lowPassHz];
  [self readOptionalDoubleFrom:options method:@"playChord" key:@"pulseWidth"
                  defaultValue:0.5 min:0.05 max:0.95 out:&pulseWidth];
  [self readOptionalIntegerFrom:options method:@"playChord" key:@"bitDepth"
                   defaultValue:0 min:0 max:16 out:&bitDepth];
  double chordWavetable[WAVETABLE_MAX_SAMPLES] = {0};
  int chordWavetableSize = 0;
  CopyWaveTableFromOptions(options, NULL, 0, chordWavetable, &chordWavetableSize);
  ExpressiveParams expressive = ReadExpressiveParams(options, ExpressiveDefaults());

  if (attackMs >= durationMs) attackMs = MAX(0, durationMs - 1);
  NSInteger maxRelease = MAX(0, durationMs - attackMs);
  if (releaseMs > maxRelease) releaseMs = maxRelease;

  int durSamples     = MAX(1, MsToSamples((int)durationMs, engineSampleRate));
  int attackSamples  = MAX(1, MsToSamples((int)attackMs,   engineSampleRate));
  int releaseSamples = MAX(1, MsToSamples((int)releaseMs,  engineSampleRate));
  if (attackSamples  > MAX(0, durSamples - 1))             attackSamples  = MAX(0, durSamples - 1);
  if (releaseSamples > MAX(0, durSamples - attackSamples)) releaseSamples = MAX(0, durSamples - attackSamples);

  // Per-voice pans override
  NSArray *pansOverride = nil;
  if ([self containsRawKey:options key:@"pans"]) {
    id raw = [options objectForKey:@"pans"];
    if ([raw isKindOfClass:[NSArray class]]) pansOverride = (NSArray *)raw;
  }

  // Per-voice detune in cents
  NSArray *detunes = nil;
  if ([self containsRawKey:options key:@"detunes"]) {
    id raw = [options objectForKey:@"detunes"];
    if ([raw isKindOfClass:[NSArray class]]) detunes = (NSArray *)raw;
  }

  NSInteger count = (NSInteger)freqArr.count;
  SynthCommand chordCommands[ABSOLUTE_MAX_VOICES];
  for (NSInteger i = 0; i < count; i++) {
    double freq = [freqArr[i] doubleValue];
    if (detunes && i < (NSInteger)detunes.count) {
      double cents = 0.0;
      if (ParseFiniteDoubleFromAny(detunes[i], &cents)) freq = DetunedFrequency(freq, cents, maxFreq);
    }
    double pan  = 0.0;
    if (pansOverride && i < (NSInteger)pansOverride.count) {
      double pv = 0.0;
      if (ParseFiniteDoubleFromAny(pansOverride[i], &pv)) pan = ClampDouble(pv, -1.0, 1.0);
    } else if (panSpread > 0.0 && count > 1) {
      pan = panSpread * (2.0 * i / (count - 1) - 1.0);
    }

    SynthCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type           = CMD_NOTE_ON;
    cmd.frequency      = freq;
    cmd.durationSamples= durSamples;
    cmd.attackSamples  = attackSamples;
    cmd.releaseSamples = releaseSamples;
    cmd.volume         = volume;
    cmd.eventVoiceCount = (int)count;
    cmd.waveType       = (int)waveType;
    cmd.pan            = pan;
    cmd.panEnd         = pan;
    cmd.noiseRate      = noiseRate;
    cmd.lowPassHz      = lowPassHz;
    cmd.pulseWidth     = pulseWidth;
    cmd.bitDepth       = (int)bitDepth;
    cmd.wavetableSize  = chordWavetableSize;
    if (chordWavetableSize > 0) {
      memcpy(cmd.wavetable, chordWavetable,
             chordWavetableSize * sizeof(double));
    }
    ApplyExpressiveParams(&cmd, &expressive, engineSampleRate);

    chordCommands[i] = cmd;
  }

  // Publish the chord with one release-store so the audio callback observes
  // every note together or none of them.
  os_unfair_lock_lock(&commandProducerLock);
  uint32_t write = atomic_load_explicit(&commandWriteIndex, memory_order_relaxed);
  uint32_t read = atomic_load_explicit(&commandReadIndex, memory_order_acquire);
  uint32_t used = (write + COMMAND_QUEUE_CAPACITY - read) % COMMAND_QUEUE_CAPACITY;
  uint32_t available = COMMAND_QUEUE_CAPACITY - 1 - used;
  BOOL dropped = (uint32_t)count > available;
  if (!dropped) {
    for (NSInteger i = 0; i < count; i++) {
      commandQueue[write] = chordCommands[i];
      write = (write + 1) % COMMAND_QUEUE_CAPACITY;
    }
    atomic_store_explicit(&commandWriteIndex, write, memory_order_release);
  }
  os_unfair_lock_unlock(&commandProducerLock);

  if (dropped) {
    [self warnOnceMethod:@"playChord" key:@"queue" reason:@"overflow"
                   value:@"command_queue_full" action:@"drop_newest"];
  }
  if(dropped) __atomic_fetch_add(&statsRejected[2],1,__ATOMIC_RELAXED);
  return @(!dropped);
  }
}

- (NSNumber *)playPattern:(id)args
{
  @synchronized(self) {
  if (audioEngine == nil || engineMaxVoices < 1) { __atomic_fetch_add(&statsRejected[1],1,__ATOMIC_RELAXED); return @NO; }

  id raw=[args isKindOfClass:[NSArray class]] && [args count]==1 ? args[0] : args;
  if(![raw isKindOfClass:[NSDictionary class]]) { __atomic_fetch_add(&statsRejected[0],1,__ATOMIC_RELAXED); return @NO; }
  NSDictionary *options=raw;
  if (![self acceptValidation:[TiSynthengineContract validatePattern:options
                                                           maxFrequency:[self maxFrequencyHz]]
                       method:@"playPattern"]) return @NO;
  id stepsObj = [options objectForKey:@"steps"];
  if (![stepsObj isKindOfClass:[NSArray class]]) return @NO;
  NSArray *steps = (NSArray *)stepsObj;
  if (steps.count == 0) return @NO;
  NSUInteger stepCount = MIN(steps.count, (NSUInteger)PATTERN_MAX_STEPS);
  if (steps.count > PATTERN_MAX_STEPS) {
    [self warnOnceMethod:@"playPattern" key:@"steps" reason:@"too_many_steps"
                   value:@(steps.count) action:@"truncated_to_512"];
  }

  uint32_t generation = atomic_fetch_add_explicit(&patternGeneration, 1,
                                                   memory_order_relaxed) + 1;
  // The previous pattern, if it had not ended, is replaced by this one.
  if (atomic_exchange_explicit(&pendingPatternGeneration, generation,
                               memory_order_acq_rel) != 0) {
    [self firePatternEnd:NO];
  }
  double bpm = 120.0;
  double swing = 0.0;
  id bpmObj = [options objectForKey:@"bpm"];
  if (bpmObj) {
    double bv = 0.0;
    if (ParseFiniteDoubleFromAny(bpmObj, &bv)) bpm = ClampDouble(bv, 20.0, 300.0);
  }
  id swingObj = [options objectForKey:@"swing"];
  if (swingObj) {
    double sv = 0.0;
    if (ParseFiniteDoubleFromAny(swingObj, &sv)) swing = ClampDouble(sv, -0.49, 0.49);
  }

  double quarter = 60000.0 / bpm;
  double maxFreq = [self maxFrequencyHz];
  id globalNoteValue = [options objectForKey:@"noteValue"];
  double globalStepDuration = 0.0;
  BOOL hasGlobalStepDuration = ParseFiniteDoubleFromAny(
      [options objectForKey:@"stepDuration"], &globalStepDuration)
      && globalStepDuration > 0.0;

  NSInteger globalAttack = ATTACK_DEFAULT_MS;
  NSInteger globalRelease = RELEASE_DEFAULT_MS;
  NSInteger globalWaveType = 0;
  double globalVolume = VOLUME_DEFAULT;
  double globalPan = 0.0;
  double globalPanEnd = 0.0;
  double globalFrequencyEnd = 0.0;
  double globalLfoFreq = 0.0;
  double globalLfoDepth = 0.0;
  double globalNoiseRate = 0.0;
  double globalNoiseRateEnd = 0.0;
  double globalLowPassHz = 0.0;
  double globalPulseWidth = 0.5;
  double globalPulseWidthEnd = 0.0;
  NSInteger globalBitDepth = 0;
  double globalWavetable[WAVETABLE_MAX_SAMPLES] = {0};
  int globalWavetableSize = 0;
  [self readOptionalIntegerFrom:options method:@"playPattern" key:@"attack"
                   defaultValue:ATTACK_DEFAULT_MS min:ATTACK_MIN_MS max:ATTACK_MAX_MS
                            out:&globalAttack];
  [self readOptionalIntegerFrom:options method:@"playPattern" key:@"release"
                   defaultValue:RELEASE_DEFAULT_MS min:RELEASE_MIN_MS max:RELEASE_MAX_MS
                            out:&globalRelease];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"volume"
                  defaultValue:VOLUME_DEFAULT min:VOLUME_MIN max:VOLUME_MAX
                           out:&globalVolume];
  [self readOptionalIntegerFrom:options method:@"playPattern" key:@"waveType"
                   defaultValue:0 min:0 max:5 out:&globalWaveType];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"pan"
                  defaultValue:0.0 min:-1.0 max:1.0 out:&globalPan];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"panEnd"
                  defaultValue:globalPan min:-1.0 max:1.0 out:&globalPanEnd];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"frequencyEnd"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&globalFrequencyEnd];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"lfoFreq"
                  defaultValue:0.0 min:0.0 max:20000.0 out:&globalLfoFreq];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"lfoDepth"
                  defaultValue:0.0 min:0.0 max:20000.0 out:&globalLfoDepth];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"noiseRate"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&globalNoiseRate];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"noiseRateEnd"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&globalNoiseRateEnd];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"lowPassHz"
                  defaultValue:0.0 min:0.0 max:maxFreq out:&globalLowPassHz];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"pulseWidth"
                  defaultValue:0.5 min:0.05 max:0.95 out:&globalPulseWidth];
  [self readOptionalDoubleFrom:options method:@"playPattern" key:@"pulseWidthEnd"
                  defaultValue:0.0 min:0.0 max:0.95 out:&globalPulseWidthEnd];
  [self readOptionalIntegerFrom:options method:@"playPattern" key:@"bitDepth"
                   defaultValue:0 min:0 max:16 out:&globalBitDepth];
  CopyWaveTableFromOptions(options, NULL, 0, globalWavetable,
                           &globalWavetableSize);
  ExpressiveParams globalExpressive = ReadExpressiveParams(options, ExpressiveDefaults());

  dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0);
  double cumulativeMs = 0.0;

  for (NSUInteger i = 0; i < stepCount; i++) {
    id stepObj = steps[i];
    NSDictionary *step = nil;
    if ([stepObj isKindOfClass:[NSDictionary class]]) {
      step = (NSDictionary *)stepObj;
    }
    if (step == nil) continue;

    double stepMs = 100.0;
    double numericTiming = 0.0;
    id durationObj = [step objectForKey:@"duration"];
    id stepNoteValue = [step objectForKey:@"noteValue"];
    if (ParseFiniteDoubleFromAny(durationObj, &numericTiming) && numericTiming > 0.0) {
      stepMs = numericTiming;
    } else if (stepNoteValue != nil) {
      stepMs = ComputeNoteValueMs([stepNoteValue description], quarter);
    } else if (globalNoteValue != nil) {
      stepMs = ComputeNoteValueMs([globalNoteValue description], quarter);
    } else if (hasGlobalStepDuration) {
      stepMs = globalStepDuration;
    }
    stepMs = ClampDouble(stepMs, DURATION_MIN_MS, DURATION_MAX_MS);

    double swingOffsetMs = 0.0;
    if (swing != 0.0) {
      swingOffsetMs = (i % 2 == 0) ? swing * stepMs : -swing * stepMs;
    }

    double freq   = 0.0;
    BOOL   isRest = YES;
    id freqObj = [step objectForKey:@"frequency"];
    id noteObj = [step objectForKey:@"note"];
    BOOL explicitRest = [[step objectForKey:@"rest"] boolValue];
    if (!explicitRest && freqObj) {
      double fv = 0.0;
      if (ParseFiniteDoubleFromAny(freqObj, &fv) && fv >= MIN_FREQUENCY_HZ) {
        freq   = ClampDouble(fv, MIN_FREQUENCY_HZ, maxFreq);
        isRest = NO;
      }
    } else if (!explicitRest && noteObj) {
      NSString *noteStr = [noteObj isKindOfClass:[NSString class]]
                        ? (NSString *)noteObj : [noteObj description];
      double f = ParseNoteToFrequency(noteStr);
      if (f >= MIN_FREQUENCY_HZ) {
        freq   = ClampDouble(f, MIN_FREQUENCY_HZ, maxFreq);
        isRest = NO;
      }
    }

    if (!isRest) {
      int dur = (int)llround(stepMs);
      NSInteger attackMs = globalAttack;
      NSInteger releaseMs = globalRelease;
      NSInteger waveType = globalWaveType;
      double volume = globalVolume;
      double pan = globalPan;
      double panEnd = globalPanEnd;
      double frequencyEnd = globalFrequencyEnd;
      double lfoFreq = globalLfoFreq;
      double lfoDepth = globalLfoDepth;
      double noiseRate = globalNoiseRate;
      double noiseRateEnd = globalNoiseRateEnd;
      double lowPassHz = globalLowPassHz;
      double pulseWidth = globalPulseWidth;
      double pulseWidthEnd = globalPulseWidthEnd;
      NSInteger bitDepth = globalBitDepth;
      [self readOptionalIntegerFrom:step method:@"step" key:@"attack"
                       defaultValue:globalAttack min:ATTACK_MIN_MS max:ATTACK_MAX_MS out:&attackMs];
      [self readOptionalIntegerFrom:step method:@"step" key:@"release"
                       defaultValue:globalRelease min:RELEASE_MIN_MS max:RELEASE_MAX_MS out:&releaseMs];
      [self readOptionalDoubleFrom:step method:@"step" key:@"volume"
                      defaultValue:globalVolume min:VOLUME_MIN max:VOLUME_MAX out:&volume];
      [self readOptionalIntegerFrom:step method:@"step" key:@"waveType"
                       defaultValue:globalWaveType min:0 max:5 out:&waveType];
      [self readOptionalDoubleFrom:step method:@"step" key:@"pan"
                      defaultValue:globalPan min:-1.0 max:1.0 out:&pan];
      [self readOptionalDoubleFrom:step method:@"step" key:@"panEnd"
                      defaultValue:globalPanEnd min:-1.0 max:1.0 out:&panEnd];
      [self readOptionalDoubleFrom:step method:@"step" key:@"frequencyEnd"
                      defaultValue:globalFrequencyEnd min:0.0 max:maxFreq out:&frequencyEnd];
      [self readOptionalDoubleFrom:step method:@"step" key:@"lfoFreq"
                      defaultValue:globalLfoFreq min:0.0 max:20000.0 out:&lfoFreq];
      [self readOptionalDoubleFrom:step method:@"step" key:@"lfoDepth"
                      defaultValue:globalLfoDepth min:0.0 max:20000.0 out:&lfoDepth];
      [self readOptionalDoubleFrom:step method:@"step" key:@"noiseRate"
                      defaultValue:globalNoiseRate min:0.0 max:maxFreq out:&noiseRate];
      [self readOptionalDoubleFrom:step method:@"step" key:@"noiseRateEnd"
                      defaultValue:globalNoiseRateEnd min:0.0 max:maxFreq out:&noiseRateEnd];
      [self readOptionalDoubleFrom:step method:@"step" key:@"lowPassHz"
                      defaultValue:globalLowPassHz min:0.0 max:maxFreq out:&lowPassHz];
      [self readOptionalDoubleFrom:step method:@"step" key:@"pulseWidth"
                      defaultValue:globalPulseWidth min:0.05 max:0.95 out:&pulseWidth];
      [self readOptionalDoubleFrom:step method:@"step" key:@"pulseWidthEnd"
                      defaultValue:globalPulseWidthEnd min:0.0 max:0.95 out:&pulseWidthEnd];
      [self readOptionalIntegerFrom:step method:@"step" key:@"bitDepth"
                       defaultValue:globalBitDepth min:0 max:16 out:&bitDepth];
      ExpressiveParams expressive = ReadExpressiveParams(step, globalExpressive);
      freq = DetunedFrequency(freq, expressive.detune, maxFreq);
      if (frequencyEnd > 0.0) {
        frequencyEnd = DetunedFrequency(frequencyEnd, expressive.detune, maxFreq);
      }

      if (attackMs >= dur) attackMs = MAX(0, dur - 1);
      NSInteger maxRel = MAX(0, dur - attackMs);
      if (releaseMs > maxRel) releaseMs = maxRel;

      int durSamples     = MAX(1, MsToSamples(dur,           engineSampleRate));
      int attackSamples  = MAX(1, MsToSamples((int)attackMs, engineSampleRate));
      int releaseSamples = MAX(1, MsToSamples((int)releaseMs,engineSampleRate));
      if (attackSamples  > MAX(0, durSamples - 1))             attackSamples  = MAX(0, durSamples - 1);
      if (releaseSamples > MAX(0, durSamples - attackSamples)) releaseSamples = MAX(0, durSamples - attackSamples);

      SynthCommand cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type           = CMD_NOTE_ON;
      cmd.frequency      = freq;
      cmd.durationSamples= durSamples;
      cmd.attackSamples  = attackSamples;
      cmd.releaseSamples = releaseSamples;
      cmd.volume         = volume;
      cmd.waveType       = (int)waveType;
      cmd.pan            = pan;
      cmd.panEnd         = panEnd;
      cmd.frequencyEnd   = frequencyEnd;
      cmd.lfoFreq        = lfoFreq;
      cmd.lfoDepth       = lfoDepth;
      cmd.noiseRate      = noiseRate;
      cmd.noiseRateEnd   = noiseRateEnd;
      cmd.lowPassHz      = lowPassHz;
      cmd.pulseWidth     = pulseWidth;
      cmd.pulseWidthEnd  = pulseWidthEnd;
      cmd.bitDepth       = (int)bitDepth;
      CopyWaveTableFromOptions(step, globalWavetable, globalWavetableSize,
                               cmd.wavetable, &cmd.wavetableSize);
      ApplyExpressiveParams(&cmd, &expressive, engineSampleRate);

      double schedMs = cumulativeMs + swingOffsetMs;
      if (schedMs < 0.0) schedMs = 0.0;

      int64_t delayNs = (int64_t)(schedMs * 1e6);
      __weak TiSynthengineModule *weakSelf = self;

      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayNs), q, ^{
        TiSynthengineModule *owner=weakSelf;
        if(owner == nil) return;
        @synchronized(owner) {
        // Check generation — skip if a newer pattern has been started
        if (atomic_load_explicit(&owner->patternGeneration, memory_order_relaxed)
            != generation) return;
        if(owner->audioEngine == nil) return;
        BOOL dropped = [owner enqueueCommand:cmd];
        if (dropped) {
          __atomic_fetch_add(&owner->droppedPatternSteps,1,__ATOMIC_RELAXED);
          [owner warnOnceMethod:@"playPattern" key:@"queue" reason:@"overflow"
                             value:@"command_queue_full" action:@"drop_newest"];
        }
        }
      });
    }

    cumulativeMs += stepMs;
  }

  // The same end instant Android uses: the summed step lengths, rounded up, plus 1 ms.
  int64_t endDelayNs = ((int64_t)ceil(cumulativeMs) + 1) * 1000000;
  __weak TiSynthengineModule *endSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, endDelayNs), dispatch_get_main_queue(), ^{
    TiSynthengineModule *module = endSelf;
    if (module == nil) return;
    uint32_t expected = generation;
    if (atomic_compare_exchange_strong_explicit(&module->pendingPatternGeneration, &expected, 0,
                                                memory_order_acq_rel, memory_order_relaxed)) {
      [module fireEvent:@"patternend" withObject:@{ @"completed": @YES }];
    }
  });
  return @YES;
  }
}

- (void)stopAll:(id)args
{
  @synchronized(self) {
#pragma unused(args)
  if (audioEngine == nil) return;
  BOOL dropped = [self enqueueSimpleCommand:CMD_STOP_ALL];
  if (dropped) {
    [self warnOnceMethod:@"stopAll" key:@"queue" reason:@"overflow"
                   value:@"command_queue_full" action:@"fallback_control_flag"];
  }
  }
}

- (void)shutdown:(id)args
{
  @synchronized(self) {
#pragma unused(args)
  if (audioEngine == nil) return;

  // Cancel any pending pattern dispatches; a closing owner gets no patternend
  atomic_fetch_add_explicit(&patternGeneration, 1, memory_order_relaxed);
  atomic_store_explicit(&pendingPatternGeneration, 0, memory_order_release);

  BOOL dropped = [self enqueueSimpleCommand:CMD_SHUTDOWN];
  if (dropped) {
    [self warnOnceMethod:@"shutdown" key:@"queue" reason:@"overflow"
                   value:@"command_queue_full" action:@"fallback_control_flag"];
  }

  [audioEngine stop];
  synth_stats_stopped(&stats);
  sourceNode  = nil;
  audioEngine = nil;

  if (lookaheadL) { free(lookaheadL); lookaheadL = NULL; }
  if (lookaheadR) { free(lookaheadR); lookaheadR = NULL; }
  if (lookaheadPeaks) { free(lookaheadPeaks); lookaheadPeaks = NULL; }
  if (lookaheadPeakIndices) {
    free(lookaheadPeakIndices);
    lookaheadPeakIndices = NULL;
  }
  atomic_store_explicit(&commandWriteIndex, 0, memory_order_relaxed);
  atomic_store_explicit(&commandReadIndex,  0, memory_order_relaxed);
  }
}

// ---------------------------------------------------------------------------
#pragma mark - Private helpers
// ---------------------------------------------------------------------------

// Delivered on the main queue, like the natural-end event, so listeners always
// run in the same context.
- (void)firePatternEnd:(BOOL)completed
{
  __weak TiSynthengineModule *weakSelf = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    [weakSelf fireEvent:@"patternend" withObject:@{ @"completed": @(completed) }];
  });
}

// Parse "frequencies" or "notes" array from options dict → NSArray of NSNumber
- (NSArray *)parseFreqArray:(NSDictionary *)options method:(NSString *)method maxFreq:(double)maxFreq
{
  NSArray *raw = nil;
  if ([self containsRawKey:options key:@"frequencies"]) {
    id val = [options objectForKey:@"frequencies"];
    if ([val isKindOfClass:[NSArray class]]) raw = (NSArray *)val;
  } else if ([self containsRawKey:options key:@"notes"]) {
    id val = [options objectForKey:@"notes"];
    if ([val isKindOfClass:[NSArray class]]) raw = (NSArray *)val;
  }

  if (!raw) return nil;

  NSMutableArray *result = [NSMutableArray arrayWithCapacity:raw.count];
  for (id item in raw) {
    double f = 0.0;
    if (ParseFiniteDoubleFromAny(item, &f)) {
      [result addObject:@(ClampDouble(f, MIN_FREQUENCY_HZ, maxFreq))];
    } else {
      NSString *noteStr = [item isKindOfClass:[NSString class]]
                        ? (NSString *)item : [item description];
      double parsed = ParseNoteToFrequency(noteStr);
      double final  = parsed >= MIN_FREQUENCY_HZ
                    ? ClampDouble(parsed, MIN_FREQUENCY_HZ, maxFreq) : MIN_FREQUENCY_HZ;
      [result addObject:@(final)];
    }
  }
  return result;
}

@end
