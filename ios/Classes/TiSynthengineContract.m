#import "TiSynthengineContract.h"
#import <math.h>

static const NSInteger ContractDefaultMaxVoices = 8;
static NSString *const ContractDefaultMixingProfile = @"conservative";
static NSString *const ContractDefaultAudioSession = @"playback";
static const double ContractDefaultFrequency = 440.0;
static const NSInteger ContractDefaultDuration = 500;
static const NSInteger ContractDefaultAttack = 50;
static const NSInteger ContractDefaultRelease = 50;
static const double ContractDefaultVolume = 1.0;
static const double ContractDefaultNoiseRate = 0.0;
static const double ContractDefaultLowPass = 0.0;
static const double ContractDefaultPulseWidth = 0.5;
static const NSInteger ContractDefaultBitDepth = 0;
static const NSInteger ContractMaxVoices = 32;
static const NSInteger ContractMaxPatternSteps = 512;
static const NSInteger ContractMinWavetableSamples = 2;
static const NSInteger ContractMaxWavetableSamples = 64;

static BOOL HasKey(NSDictionary *options, NSString *key)
{
  return options != nil && [options objectForKey:key] != nil;
}

static NSDictionary *Failure(NSString *key, NSString *reason, id value)
{
  return @{
    @"key": key ?: @"options",
    @"reason": reason ?: @"invalid",
    @"value": value ?: [NSNull null]
  };
}

static BOOL FiniteNumber(id raw, double *outValue)
{
  if (![raw isKindOfClass:[NSNumber class]]) return NO;
  if (CFGetTypeID((__bridge CFTypeRef)raw) == CFBooleanGetTypeID()) return NO;
  double value = [(NSNumber *)raw doubleValue];
  if (!isfinite(value)) return NO;
  if (outValue) *outValue = value;
  return YES;
}

static NSDictionary *OptionalDouble(NSDictionary *options, NSString *key,
                                    double minValue, double maxValue)
{
  if (!HasKey(options, key)) return nil;
  id raw = [options objectForKey:key];
  double value = 0.0;
  if (!FiniteNumber(raw, &value)) return Failure(key, @"non_numeric_or_non_finite", raw);
  if (value < minValue || value > maxValue) return Failure(key, @"out_of_range", raw);
  return nil;
}

static NSDictionary *OptionalInteger(NSDictionary *options, NSString *key,
                                     NSInteger minValue, NSInteger maxValue)
{
  if (!HasKey(options, key)) return nil;
  id raw = [options objectForKey:key];
  double value = 0.0;
  if (!FiniteNumber(raw, &value)) return Failure(key, @"non_numeric_or_non_finite", raw);
  double rounded = rint(value);
  if (fabs(value - rounded) > 1e-9) return Failure(key, @"not_integer", raw);
  if (rounded < minValue || rounded > maxValue) return Failure(key, @"out_of_range", raw);
  return nil;
}

static NSDictionary *OptionalWaveTable(NSDictionary *options, NSString *key)
{
  if (!HasKey(options, key)) return nil;
  id raw = [options objectForKey:key];
  if (![raw isKindOfClass:[NSArray class]]) return Failure(key, @"not_array", raw);
  NSArray *values = (NSArray *)raw;
  if (values.count < ContractMinWavetableSamples || values.count > ContractMaxWavetableSamples) {
    return Failure(key, @"invalid_sample_count", @(values.count));
  }
  for (id value in values) {
    NSDictionary *failure = OptionalDouble(@{ key: value ?: [NSNull null] }, key, -1.0, 1.0);
    if (failure) return failure;
  }
  return nil;
}

static NSDictionary *OptionalBitDepth(NSDictionary *options, NSString *key)
{
  if (!HasKey(options, key)) return nil;
  NSDictionary *failure = OptionalInteger(options, key, 0, 16);
  if (failure) return failure;
  NSInteger value = [[options objectForKey:key] integerValue];
  return value == 0 || value >= 2 ? nil : Failure(key, @"out_of_range", @(value));
}

static NSDictionary *OptionalPulseWidthEnd(NSDictionary *options, NSString *key)
{
  if (!HasKey(options, key)) return nil;
  id raw = [options objectForKey:key];
  double value = 0.0;
  if (!FiniteNumber(raw, &value)) return Failure(key, @"non_numeric_or_non_finite", raw);
  return value == 0.0 || (value >= 0.05 && value <= 0.95)
       ? nil : Failure(key, @"out_of_range", raw);
}

static double StrictNoteFrequency(id raw)
{
  if (![raw isKindOfClass:[NSString class]]) return -1.0;
  NSString *value = [(NSString *)raw stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if (value.length == 0) return -1.0;
  NSString *upper = [value uppercaseString];
  NSUInteger position = 0;
  NSInteger semitone = 0;

  if ([upper hasPrefix:@"SOL"]) { semitone = 7; position = 3; }
  else if ([upper hasPrefix:@"DO"]) { semitone = 0; position = 2; }
  else if ([upper hasPrefix:@"RE"]) { semitone = 2; position = 2; }
  else if ([upper hasPrefix:@"MI"]) { semitone = 4; position = 2; }
  else if ([upper hasPrefix:@"FA"]) { semitone = 5; position = 2; }
  else if ([upper hasPrefix:@"LA"]) { semitone = 9; position = 2; }
  else if ([upper hasPrefix:@"SI"] || [upper hasPrefix:@"TI"]) { semitone = 11; position = 2; }
  else {
    position = 1;
    switch ([upper characterAtIndex:0]) {
      case 'C': semitone = 0; break;
      case 'D': semitone = 2; break;
      case 'E': semitone = 4; break;
      case 'F': semitone = 5; break;
      case 'G': semitone = 7; break;
      case 'A': semitone = 9; break;
      case 'B': semitone = 11; break;
      default: return -1.0;
    }
  }

  if (position < value.length) {
    unichar accidental = [value characterAtIndex:position];
    if (accidental == '#') { semitone += 1; position += 1; }
    else if (accidental == 'b') { semitone -= 1; position += 1; }
  }

  NSInteger octave = 4;
  if (position < value.length) {
    unichar digit = [value characterAtIndex:position];
    if (digit < '0' || digit > '9' || position + 1 != value.length) return -1.0;
    octave = digit - '0';
  }
  NSInteger midi = (octave + 1) * 12 + semitone;
  return 440.0 * pow(2.0, (midi - 69.0) / 12.0);
}

static NSDictionary *ValidNote(id raw, NSString *key)
{
  if (![raw isKindOfClass:[NSString class]]) return Failure(key, @"not_string", raw);
  return StrictNoteFrequency(raw) >= 1.0 ? nil : Failure(key, @"invalid_note", raw);
}

static NSDictionary *RejectUnknownOptions(NSDictionary *options, NSArray *supportedKeys)
{
  for (id rawKey in options) {
    if (![supportedKeys containsObject:rawKey]) {
      NSString *key = [rawKey isKindOfClass:[NSString class]] ? rawKey : [rawKey description];
      return Failure(key, @"unsupported_option", [options objectForKey:rawKey]);
    }
  }
  return nil;
}

static NSDictionary *ValidateVoiceOptions(NSDictionary *options, double maxFrequency,
                                          BOOL allowSweep)
{
  NSDictionary *failure = OptionalInteger(options, @"duration", 1, 60000);
  if (failure) return failure;
  failure = OptionalInteger(options, @"attack", 0, 2000);
  if (failure) return failure;
  failure = OptionalInteger(options, @"release", 0, 4000);
  if (failure) return failure;
  failure = OptionalDouble(options, @"volume", 0.0, 2.0);
  if (failure) return failure;
  failure = OptionalInteger(options, @"waveType", 0, 5);
  if (failure) return failure;
  failure = OptionalDouble(options, @"pan", -1.0, 1.0);
  if (failure) return failure;
  failure = OptionalDouble(options, @"noiseRate", 0.0, maxFrequency);
  if (failure) return failure;
  failure = OptionalDouble(options, @"lowPassHz", 0.0, maxFrequency);
  if (failure) return failure;
  failure = OptionalDouble(options, @"pulseWidth", 0.05, 0.95);
  if (failure) return failure;
  failure = OptionalWaveTable(options, @"waveTable");
  if (failure) return failure;
  failure = OptionalBitDepth(options, @"bitDepth");
  if (failure) return failure;
  if (allowSweep) {
    failure = OptionalDouble(options, @"panEnd", -1.0, 1.0);
    if (failure) return failure;
    failure = OptionalDouble(options, @"frequencyEnd", 0.0, maxFrequency);
    if (failure) return failure;
    failure = OptionalDouble(options, @"lfoFreq", 0.0, 20000.0);
    if (failure) return failure;
    failure = OptionalDouble(options, @"lfoDepth", 0.0, 20000.0);
    if (failure) return failure;
    failure = OptionalDouble(options, @"noiseRateEnd", 0.0, maxFrequency);
    if (failure) return failure;
    failure = OptionalPulseWidthEnd(options, @"pulseWidthEnd");
    if (failure) return failure;
  }
  return nil;
}

static NSInteger IntegerValue(NSDictionary *options, NSString *key, NSInteger fallback)
{
  if (!HasKey(options, key)) return fallback;
  return (NSInteger)llround([[options objectForKey:key] doubleValue]);
}

static double PatternNoteValueMs(NSString *noteValue, double quarterMs)
{
  NSString *value = noteValue;
  if ([value isEqualToString:@"1n"]) return quarterMs * 4.0;
  if ([value isEqualToString:@"2n"]) return quarterMs * 2.0;
  if ([value isEqualToString:@"2n."]) return quarterMs * 3.0;
  if ([value isEqualToString:@"4n"]) return quarterMs;
  if ([value isEqualToString:@"4n."]) return quarterMs * 1.5;
  if ([value isEqualToString:@"8n"]) return quarterMs * 0.5;
  if ([value isEqualToString:@"8n."]) return quarterMs * 0.75;
  if ([value isEqualToString:@"8t"]) return quarterMs / 3.0;
  if ([value isEqualToString:@"16n"]) return quarterMs * 0.25;
  if ([value isEqualToString:@"16t"]) return quarterMs / 6.0;
  return quarterMs;
}

static double PatternStepDuration(NSDictionary *pattern, NSDictionary *step)
{
  if (HasKey(step, @"duration")) return [[step objectForKey:@"duration"] doubleValue];
  double bpm = HasKey(pattern, @"bpm") ? [[pattern objectForKey:@"bpm"] doubleValue] : 120.0;
  double quarterMs = 60000.0 / bpm;
  if (HasKey(step, @"noteValue")) {
    return PatternNoteValueMs([step objectForKey:@"noteValue"], quarterMs);
  }
  if (HasKey(pattern, @"noteValue")) {
    return PatternNoteValueMs([pattern objectForKey:@"noteValue"], quarterMs);
  }
  if (HasKey(pattern, @"stepDuration")) {
    return [[pattern objectForKey:@"stepDuration"] doubleValue];
  }
  return 100.0;
}

static NSDictionary *ValidatePatternEnvelope(NSDictionary *pattern, NSDictionary *step)
{
  NSInteger duration = (NSInteger)llround(PatternStepDuration(pattern, step));
  BOOL attackExplicit = HasKey(step, @"attack") || HasKey(pattern, @"attack");
  id attackValue = HasKey(step, @"attack")
                 ? [step objectForKey:@"attack"] : [pattern objectForKey:@"attack"];
  NSInteger attack = HasKey(step, @"attack")
                   ? IntegerValue(step, @"attack", ContractDefaultAttack)
                   : IntegerValue(pattern, @"attack", ContractDefaultAttack);
  if (attackExplicit && attack >= duration) {
    return Failure(@"attack", @"attack_greater_or_equal_duration", attackValue);
  }

  BOOL releaseExplicit = HasKey(step, @"release") || HasKey(pattern, @"release");
  id releaseValue = HasKey(step, @"release")
                  ? [step objectForKey:@"release"] : [pattern objectForKey:@"release"];
  NSInteger release = HasKey(step, @"release")
                    ? IntegerValue(step, @"release", ContractDefaultRelease)
                    : IntegerValue(pattern, @"release", ContractDefaultRelease);
  if (releaseExplicit && release > MAX(0, duration - attack)) {
    return Failure(@"release", @"release_exceeds_remaining_duration", releaseValue);
  }
  return nil;
}

static NSDictionary *ValidateExplicitEnvelope(NSDictionary *options)
{
  NSInteger duration = IntegerValue(options, @"duration", ContractDefaultDuration);
  NSInteger attack = IntegerValue(options, @"attack", ContractDefaultAttack);
  if (HasKey(options, @"attack") && attack >= duration) {
    return Failure(@"attack", @"attack_greater_or_equal_duration", @(attack));
  }
  NSInteger release = IntegerValue(options, @"release", ContractDefaultRelease);
  if (HasKey(options, @"release") && release > MAX(0, duration - attack)) {
    return Failure(@"release", @"release_exceeds_remaining_duration", @(release));
  }
  return nil;
}

static NSDictionary *OptionalNoteValue(NSDictionary *options, NSString *key)
{
  if (!HasKey(options, key)) return nil;
  id raw = [options objectForKey:key];
  if (![raw isKindOfClass:[NSString class]]) return Failure(key, @"not_string", raw);
  static NSSet *supported = nil;
  if (supported == nil) {
    supported = [[NSSet alloc] initWithArray:
                 @[@"1n", @"2n", @"2n.", @"4n", @"4n.", @"8n", @"8n.", @"8t", @"16n", @"16t"]];
  }
  return [supported containsObject:[(NSString *)raw stringByTrimmingCharactersInSet:
                                    [NSCharacterSet whitespaceAndNewlineCharacterSet]]]
       ? nil : Failure(key, @"unsupported_note_value", raw);
}

@implementation TiSynthengineContract

+ (NSDictionary *)validateStartEngine:(NSDictionary *)options
{
  NSDictionary *failure = RejectUnknownOptions(options, @[@"maxVoices", @"mixingProfile",
                                                            @"audioSession"]);
  if (failure) return failure;
  failure = OptionalInteger(options, @"maxVoices", 1, ContractMaxVoices);
  if (failure) return failure;
  if (HasKey(options, @"mixingProfile")) {
    id raw = [options objectForKey:@"mixingProfile"];
    if (![raw isKindOfClass:[NSString class]]) return Failure(@"mixingProfile", @"not_string", raw);
    NSString *profile = [[(NSString *)raw stringByTrimmingCharactersInSet:
                          [NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    if (!([profile isEqualToString:@"raw"] || [profile isEqualToString:@"conservative"]
          || [profile isEqualToString:@"balanced"] || [profile isEqualToString:@"speakersafe"])) {
      return Failure(@"mixingProfile", @"unsupported_value", raw);
    }
  }
  if (HasKey(options, @"audioSession")) {
    id raw = [options objectForKey:@"audioSession"];
    if (![raw isKindOfClass:[NSString class]]) return Failure(@"audioSession", @"not_string", raw);
    NSString *session = [[(NSString *)raw stringByTrimmingCharactersInSet:
                          [NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    if (!([session isEqualToString:@"playback"] || [session isEqualToString:@"ambient"])) {
      return Failure(@"audioSession", @"unsupported_value", raw);
    }
  }
  return nil;
}

+ (NSDictionary *)validateSetVolume:(NSDictionary *)options
{
  NSDictionary *failure = RejectUnknownOptions(options, @[@"volume"]);
  if (failure) return failure;
  return OptionalDouble(options, @"volume", 0.0, 2.0);
}

+ (NSDictionary *)validateTone:(NSDictionary *)options maxFrequency:(double)maxFrequency
{
  NSDictionary *failure = RejectUnknownOptions(options,
    @[@"frequency", @"note", @"duration", @"attack", @"release", @"volume",
      @"waveType", @"pan", @"panEnd", @"frequencyEnd", @"lfoFreq", @"lfoDepth",
      @"noiseRate", @"noiseRateEnd", @"lowPassHz", @"pulseWidth", @"pulseWidthEnd",
      @"waveTable", @"bitDepth"]);
  if (failure) return failure;
  BOOL hasFrequency = HasKey(options, @"frequency");
  BOOL hasNote = HasKey(options, @"note");
  if (hasFrequency && hasNote) return Failure(@"frequency", @"conflicting_pitch_fields", [options objectForKey:@"frequency"]);
  if (hasFrequency && (failure = OptionalDouble(options, @"frequency", 1.0, maxFrequency))) return failure;
  if (hasNote && (failure = ValidNote([options objectForKey:@"note"], @"note"))) return failure;
  failure = ValidateVoiceOptions(options, maxFrequency, YES);
  if (!failure && IntegerValue(options, @"waveType", 0) == 5 && !HasKey(options, @"waveTable")) {
    failure = Failure(@"waveTable", @"missing_required", nil);
  }
  return failure ?: ValidateExplicitEnvelope(options);
}

+ (NSDictionary *)validateChord:(NSDictionary *)options maxFrequency:(double)maxFrequency
{
  NSDictionary *failure = RejectUnknownOptions(options,
    @[@"frequencies", @"notes", @"duration", @"attack", @"release", @"volume",
      @"waveType", @"panSpread", @"pans", @"noiseRate", @"lowPassHz",
      @"pulseWidth", @"waveTable", @"bitDepth"]);
  if (failure) return failure;
  BOOL hasFrequencies = HasKey(options, @"frequencies");
  BOOL hasNotes = HasKey(options, @"notes");
  if (hasFrequencies == hasNotes) return Failure(@"frequencies", hasFrequencies ? @"conflicting_pitch_fields" : @"missing_required", nil);
  NSString *pitchKey = hasFrequencies ? @"frequencies" : @"notes";
  id rawPitches = [options objectForKey:pitchKey];
  if (![rawPitches isKindOfClass:[NSArray class]]) return Failure(pitchKey, @"not_array", rawPitches);
  NSArray *pitches = (NSArray *)rawPitches;
  if (pitches.count < 1 || pitches.count > ContractMaxVoices) return Failure(pitchKey, @"invalid_voice_count", @(pitches.count));
  for (id pitch in pitches) {
    failure = hasFrequencies
      ? OptionalDouble(@{ @"pitch": pitch ?: [NSNull null] }, @"pitch", 1.0, maxFrequency)
      : ValidNote(pitch, @"notes");
    if (failure) return Failure(pitchKey, [failure objectForKey:@"reason"], pitch);
  }
  failure = ValidateVoiceOptions(options, maxFrequency, NO);
  if (failure) return failure;
  if (IntegerValue(options, @"waveType", 0) == 5 && !HasKey(options, @"waveTable")) {
    return Failure(@"waveTable", @"missing_required", nil);
  }
  failure = OptionalDouble(options, @"panSpread", 0.0, 1.0);
  if (failure) return failure;
  if (HasKey(options, @"pans")) {
    id rawPans = [options objectForKey:@"pans"];
    if (![rawPans isKindOfClass:[NSArray class]]) return Failure(@"pans", @"not_array", rawPans);
    NSArray *pans = (NSArray *)rawPans;
    if (pans.count > pitches.count) return Failure(@"pans", @"too_many_values", @(pans.count));
    for (id pan in pans) {
      failure = OptionalDouble(@{ @"pan": pan ?: [NSNull null] }, @"pan", -1.0, 1.0);
      if (failure) return Failure(@"pans", [failure objectForKey:@"reason"], pan);
    }
  }
  return ValidateExplicitEnvelope(options);
}

+ (NSDictionary *)validatePattern:(NSDictionary *)options maxFrequency:(double)maxFrequency
{
  NSDictionary *failure = RejectUnknownOptions(options,
    @[@"steps", @"bpm", @"swing", @"stepDuration", @"noteValue", @"attack",
      @"release", @"volume", @"waveType", @"pan", @"panEnd",
      @"frequencyEnd", @"lfoFreq", @"lfoDepth", @"noiseRate", @"noiseRateEnd",
      @"lowPassHz", @"pulseWidth", @"pulseWidthEnd", @"waveTable", @"bitDepth"]);
  if (failure) return failure;
  id rawSteps = [options objectForKey:@"steps"];
  if (![rawSteps isKindOfClass:[NSArray class]]) return Failure(@"steps", @"not_array", rawSteps);
  NSArray *steps = (NSArray *)rawSteps;
  if (steps.count < 1 || steps.count > ContractMaxPatternSteps) return Failure(@"steps", @"invalid_step_count", @(steps.count));
  failure = OptionalDouble(options, @"bpm", 20.0, 300.0);
  if (failure) return failure;
  failure = OptionalDouble(options, @"swing", -0.49, 0.49);
  if (failure) return failure;
  failure = OptionalDouble(options, @"stepDuration", 1.0, 60000.0);
  if (failure) return failure;
  failure = OptionalNoteValue(options, @"noteValue");
  if (failure) return failure;
  failure = ValidateVoiceOptions(options, maxFrequency, YES);
  if (failure) return failure;

  NSInteger globalWaveType = IntegerValue(options, @"waveType", 0);
  BOOL globalHasWaveTable = HasKey(options, @"waveTable");

  for (id rawStep in steps) {
    if (![rawStep isKindOfClass:[NSDictionary class]]) return Failure(@"steps", @"invalid_step_type", rawStep);
    NSDictionary *step = (NSDictionary *)rawStep;
    failure = RejectUnknownOptions(step,
      @[@"rest", @"frequency", @"note", @"duration", @"noteValue", @"attack",
        @"release", @"volume", @"waveType", @"pan", @"panEnd", @"frequencyEnd",
        @"lfoFreq", @"lfoDepth", @"noiseRate", @"noiseRateEnd", @"lowPassHz",
        @"pulseWidth", @"pulseWidthEnd", @"waveTable", @"bitDepth"]);
    if (failure) return failure;
    id restValue = [step objectForKey:@"rest"];
    if (HasKey(step, @"rest") && CFGetTypeID((__bridge CFTypeRef)restValue) != CFBooleanGetTypeID()) {
      return Failure(@"rest", @"not_boolean", restValue);
    }
    BOOL rest = [[step objectForKey:@"rest"] boolValue];
    BOOL hasFrequency = HasKey(step, @"frequency");
    BOOL hasNote = HasKey(step, @"note");
    NSInteger pitchFields = (rest ? 1 : 0) + (hasFrequency ? 1 : 0) + (hasNote ? 1 : 0);
    if (pitchFields != 1) return Failure(@"steps", pitchFields == 0 ? @"missing_pitch_or_rest" : @"conflicting_pitch_fields", step);
    if (hasFrequency && (failure = OptionalDouble(step, @"frequency", 1.0, maxFrequency))) return failure;
    if (hasNote && (failure = ValidNote([step objectForKey:@"note"], @"note"))) return failure;
    failure = OptionalDouble(step, @"duration", 1.0, 60000.0);
    if (failure) return failure;
    failure = OptionalNoteValue(step, @"noteValue");
    if (failure) return failure;
    failure = ValidateVoiceOptions(step, maxFrequency, YES);
    if (failure) return failure;
    NSInteger waveType = IntegerValue(step, @"waveType", globalWaveType);
    if (!rest && waveType == 5 && !HasKey(step, @"waveTable") && !globalHasWaveTable) {
      return Failure(@"waveTable", @"missing_required", nil);
    }
    if (!rest && (failure = ValidatePatternEnvelope(options, step))) return failure;
  }
  return nil;
}

+ (NSDictionary *)defaultsWithMaxFrequency:(double)maxFrequency
{
  return @{
    @"apiVersion": @"1.0.0",
    @"defaults": @{
      @"maxVoices": @(ContractDefaultMaxVoices),
      @"mixingProfile": ContractDefaultMixingProfile,
      @"audioSession": ContractDefaultAudioSession,
      @"frequency": @(ContractDefaultFrequency),
      @"duration": @(ContractDefaultDuration),
      @"attack": @(ContractDefaultAttack),
      @"release": @(ContractDefaultRelease),
      @"volume": @(ContractDefaultVolume),
      @"waveType": @0,
      @"pan": @0.0,
      @"panEnd": @0.0,
      @"frequencyEnd": @0.0,
      @"lfoFreq": @0.0,
      @"lfoDepth": @0.0,
      @"noiseRate": @(ContractDefaultNoiseRate),
      @"noiseRateEnd": @0.0,
      @"lowPassHz": @(ContractDefaultLowPass),
      @"pulseWidth": @(ContractDefaultPulseWidth),
      @"pulseWidthEnd": @0.0,
      @"waveTable": @[],
      @"bitDepth": @(ContractDefaultBitDepth),
      @"panSpread": @0.0,
      @"bpm": @120.0,
      @"swing": @0.0,
      @"stepDuration": @100.0
    },
    @"limits": @{
      @"maxVoices": @{ @"min": @1, @"max": @(ContractMaxVoices) },
      @"frequency": @{ @"min": @1.0, @"max": @(maxFrequency) },
      @"duration": @{ @"min": @1, @"max": @60000 },
      @"attack": @{ @"min": @0, @"max": @2000 },
      @"release": @{ @"min": @0, @"max": @4000 },
      @"volume": @{ @"min": @0.0, @"max": @2.0 },
      @"waveType": @{ @"min": @0, @"max": @5 },
      @"noiseRate": @{ @"min": @0.0, @"max": @(maxFrequency) },
      @"lowPassHz": @{ @"min": @0.0, @"max": @(maxFrequency) },
      @"pulseWidth": @{ @"min": @0.05, @"max": @0.95 },
      @"waveTableSamples": @{ @"min": @(ContractMinWavetableSamples), @"max": @(ContractMaxWavetableSamples) },
      @"bitDepth": @{ @"min": @0, @"max": @16 },
      @"patternSteps": @{ @"min": @1, @"max": @(ContractMaxPatternSteps) },
      @"pan": @{ @"min": @-1.0, @"max": @1.0 },
      @"panSpread": @{ @"min": @0.0, @"max": @1.0 },
      @"lfoFreq": @{ @"min": @0.0, @"max": @20000.0 },
      @"lfoDepth": @{ @"min": @0.0, @"max": @20000.0 },
      @"bpm": @{ @"min": @20.0, @"max": @300.0 },
      @"swing": @{ @"min": @-0.49, @"max": @0.49 },
      @"stepDuration": @{ @"min": @1, @"max": @60000 }
    },
    @"mixingProfiles": @[@"conservative", @"raw", @"balanced", @"speakerSafe"],
    @"audioSessions": @[@"playback", @"ambient"],
    @"waveTypes": @{ @"SINE": @0, @"SQUARE": @1, @"SAWTOOTH": @2, @"TRIANGLE": @3, @"NOISE": @4, @"WAVETABLE": @5 }
  };
}

@end
