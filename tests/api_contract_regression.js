const assert = require('assert')
const fs = require('fs')
const path = require('path')

const root = path.resolve(__dirname, '..')
const read = relativePath => fs.readFileSync(path.join(root, relativePath), 'utf8')
const androidModule = read('android/src/ti/synthengine/TiSynthEngineModule.java')
const androidBuildGradle = read('android/build.gradle')
const androidContract = read('android/src/ti/synthengine/SynthContract.java')
const androidDsp = read('android/src/ti/synthengine/SynthDsp.java')
const androidThread = read('android/src/ti/synthengine/SynthThread.java')
const androidNative = read('android/jni/SynthOboeEngine.cpp')
const iosHeader = read('ios/Classes/TiSynthengineModule.h')
const iosModule = read('ios/Classes/TiSynthengineModule.m')
const iosContract = read('ios/Classes/TiSynthengineContract.m')
const exampleApp = read('example/app.js')

assert(androidModule.includes('public KrollDict getDefaults()'), 'Android getDefaults')
assert(iosHeader.includes('getDefaults:(id)args'), 'iOS getDefaults')
assert(androidContract.includes('API_VERSION = "1.0.0"'), 'Android public API version')
assert(iosContract.includes('@"apiVersion": @"1.0.0"'), 'iOS public API version')
assert(androidContract.includes('DEFAULT_MIXING_PROFILE = "conservative"'), 'Android conservative default')
assert(iosContract.includes('ContractDefaultMixingProfile = @"conservative"'), 'iOS conservative default')
assert(androidModule.includes('return SynthDsp.MIXING_PROFILE_CONSERVATIVE'), 'Android runtime conservative default')
assert(iosModule.includes('mixingProfile = MIXING_PROFILE_CONSERVATIVE'), 'iOS runtime conservative default')
assert(iosContract.includes('CFBooleanGetTypeID()) return NO'), 'iOS rejects booleans as numbers')
assert(androidContract.includes('DEFAULT_AUDIO_SESSION = "playback"'), 'Android playback session default')
assert(iosContract.includes('ContractDefaultAudioSession = @"playback"'), 'iOS playback session default')
assert(androidModule.includes('result.put("audioSessions", new Object[]{"playback", "ambient"})'),
  'Android reports the audio session values')
assert(iosContract.includes('@"audioSessions": @[@"playback", @"ambient"]'),
  'iOS reports the audio session values')
assert(androidContract.includes('Validation.rejected("audioSession", "unsupported_value", raw)'),
  'Android rejects an unknown audio session')
assert(iosContract.includes('Failure(@"audioSession", @"unsupported_value", raw)'),
  'iOS rejects an unknown audio session')
assert(iosModule.includes('audioSessionCategory = AVAudioSessionCategoryPlayback;'),
  'iOS keeps Playback when audioSession is absent')
assert(iosModule.includes('[session setCategory:audioSessionCategory error:&sessionError]'),
  'iOS applies the selected audio session category')
assert(iosModule.includes('[session setCategory:self->audioSessionCategory error:&categoryError]'),
  'iOS reapplies the selected category after a route change')
assert(!androidModule.includes('options.get("audioSession")') && !androidNative.includes('audioSession'),
  'Android validates audioSession without changing its output stream')

const publicMethods = [
  'getDefaults',
  'startEngine',
  'playTone',
  'playChord',
  'playPattern',
  'setVolume',
  'stopAll',
  'shutdown'
]
const androidPublicMethods = Array.from(androidModule.matchAll(
  /@Kroll\.method\s+public\s+(?:boolean|void|KrollDict)\s+(\w+)\(/g
), match => match[1])
const iosPublicMethods = Array.from(iosHeader.matchAll(
  /- \((?:NSDictionary \*|NSNumber \*|void)\)(\w+):/g
), match => match[1])
assert.deepStrictEqual(androidPublicMethods.sort(), publicMethods.slice().sort(), 'Android public method surface')
assert.deepStrictEqual(iosPublicMethods.sort(), publicMethods.slice().sort(), 'iOS public method surface')

;['startEngine', 'setVolume', 'playTone', 'playChord', 'playPattern'].forEach((method) => {
  assert(new RegExp(`public boolean ${method}\\(`).test(androidModule), `Android ${method} returns acceptance`)
  assert(new RegExp(`\\(NSNumber \\*\\)${method}:`).test(iosModule), `iOS ${method} returns acceptance`)
})

const supportedOptions = {
  StartEngine: ['maxVoices', 'mixingProfile', 'audioSession'],
  SetVolume: ['volume'],
  Tone: [
    'frequency', 'note', 'duration', 'attack', 'release', 'volume',
    'waveType', 'pan', 'panEnd', 'frequencyEnd', 'lfoFreq', 'lfoDepth',
    'noiseRate', 'noiseRateEnd', 'lowPassHz', 'pulseWidth', 'pulseWidthEnd',
    'waveTable', 'bitDepth', 'decay', 'sustain', 'filterType', 'filterHz',
    'filterHzEnd', 'filterQ', 'fmRatio', 'fmDepth', 'fmDepthEnd', 'detune'
  ],
  Chord: [
    'frequencies', 'notes', 'duration', 'attack', 'release', 'volume',
    'waveType', 'panSpread', 'pans', 'noiseRate', 'lowPassHz', 'pulseWidth',
    'waveTable', 'bitDepth', 'decay', 'sustain', 'filterType', 'filterHz',
    'filterQ', 'fmRatio', 'fmDepth', 'detunes'
  ],
  Pattern: [
    'steps', 'bpm', 'swing', 'stepDuration', 'noteValue', 'attack',
    'release', 'volume', 'waveType', 'pan', 'panEnd',
    'frequencyEnd', 'lfoFreq', 'lfoDepth', 'noiseRate', 'noiseRateEnd',
    'lowPassHz', 'pulseWidth', 'pulseWidthEnd', 'waveTable', 'bitDepth',
    'decay', 'sustain', 'filterType', 'filterHz', 'filterHzEnd', 'filterQ',
    'fmRatio', 'fmDepth', 'fmDepthEnd', 'detune'
  ]
}
const patternStepOptions = [
  'rest', 'frequency', 'note', 'duration', 'noteValue', 'attack',
  'release', 'volume', 'waveType', 'pan', 'panEnd', 'frequencyEnd',
  'lfoFreq', 'lfoDepth', 'noiseRate', 'noiseRateEnd', 'lowPassHz',
  'pulseWidth', 'pulseWidthEnd', 'waveTable', 'bitDepth', 'decay', 'sustain',
  'filterType', 'filterHz', 'filterHzEnd', 'filterQ', 'fmRatio', 'fmDepth',
  'fmDepthEnd', 'detune'
]

function quotedValues(source, pattern, label) {
  const match = source.match(pattern)
  assert(match, `${label} supported-option declaration`)
  return Array.from(match[1].matchAll(/@?"([^"]+)"/g), value => value[1]).sort()
}

Object.keys(supportedOptions).forEach((suffix) => {
  const expected = supportedOptions[suffix].slice().sort()
  const androidOptions = quotedValues(
    androidContract,
    new RegExp(`static Validation validate${suffix}\\([\\s\\S]*?rejectUnknownOptions\\(options,([\\s\\S]*?)\\);`),
    `Android validate${suffix}`
  )
  const iosOptions = quotedValues(
    iosContract,
    new RegExp(`validate${suffix}:[\\s\\S]*?RejectUnknownOptions\\(options,([\\s\\S]*?)\\);`),
    `iOS validate${suffix}`
  )
  assert.deepStrictEqual(androidOptions, expected, `Android validate${suffix} options`)
  assert.deepStrictEqual(iosOptions, expected, `iOS validate${suffix} options`)
})

assert.deepStrictEqual(quotedValues(
  androidContract,
  /rejectUnknownOptions\(step,([\s\S]*?)\);/,
  'Android pattern step'
), patternStepOptions.slice().sort(), 'Android pattern step options')
assert.deepStrictEqual(quotedValues(
  iosContract,
  /RejectUnknownOptions\(step,([\s\S]*?)\);/,
  'iOS pattern step'
), patternStepOptions.slice().sort(), 'iOS pattern step options')

assert(androidContract.includes('"unsupported_option"'), 'Android rejects unknown options')
assert(iosContract.includes('@"unsupported_option"'), 'iOS rejects unknown options')
assert(androidContract.includes('"invalid_step_type"'), 'Android rejects invalid pattern step types')
assert(iosContract.includes('@"invalid_step_type"'), 'iOS rejects invalid pattern step types')
assert(!androidContract.includes('primitive_step_removed'), 'Android has no migration-specific step reason')
assert(!iosContract.includes('primitive_step_removed'), 'iOS has no migration-specific step reason')

;['SINE', 'SQUARE', 'SAWTOOTH', 'TRIANGLE', 'NOISE', 'WAVETABLE'].forEach((waveType) => {
  assert(androidModule.includes(`waveTypes.put("${waveType}"`), `Android ${waveType}`)
  assert(iosContract.includes(`@"${waveType}"`), `iOS ${waveType}`)
})

;['bpm', 'swing', 'stepDuration', 'panSpread', 'lfoFreq', 'lfoDepth',
  'noiseRate', 'noiseRateEnd', 'lowPassHz', 'pulseWidth', 'pulseWidthEnd',
  'waveTable', 'bitDepth'].forEach((key) => {
  assert(androidModule.includes(`defaults.put("${key}"`), `Android default ${key}`)
  assert(iosContract.includes(`@"${key}"`), `iOS default ${key}`)
})

assert(androidContract.includes('MAX_MAX_VOICES = 32'), 'Android keeps 32 voice capacity')
assert(iosContract.includes('ContractMaxVoices = 32'), 'iOS keeps 32 voice capacity')
assert(androidModule.includes('event=validation_warning'), 'Android structured warnings')
assert(iosModule.includes('event=validation_warning'), 'iOS structured warnings')
assert(androidDsp.includes('OUTPUT_CEILING = 0.85'), 'Android retains the 0.85 DSP ceiling')
assert(/#define OUTPUT_CEILING\s+0\.85\b/.test(iosModule), 'iOS retains the 0.85 DSP ceiling')
assert(androidDsp.includes('voiceGainForEvent'), 'Android has fixed per-event source gain')
assert(iosModule.includes('VoiceGainForEvent'), 'iOS has fixed per-event source gain')
assert(androidDsp.includes('/ Math.sqrt(Math.max(1, voiceCount))'),
  'Android uses constant-power chord gain')
assert(iosModule.includes('/ sqrt((double)MAX(1, voiceCount))'),
  'iOS uses constant-power chord gain')
assert(!androidDsp.includes('normalizationReleaseCoeff'),
  'Android removes signal-dependent normalization')
assert(!iosModule.includes('normalizationReleaseCoeff'),
  'iOS removes signal-dependent normalization')
assert(androidThread.includes('public boolean enqueueChord('),
  'Android exposes an atomic internal chord transaction')
assert(androidNative.includes('write_.store(write, std::memory_order_release)'),
  'Android publishes a native chord only after filling every command')
assert(androidNative.includes('oboe::AudioStreamDataCallback'),
  'Android renders from an Oboe data callback')
assert(androidNative.includes('oboe::AudioFormat::Float'),
  'Android requests a float output stream')
assert(androidNative.includes('error != oboe::Result::ErrorDisconnected'),
  'Android filters stream recovery to route disconnections')
assert(androidNative.includes('reopenAndStartLocked();'),
  'Android automatically reopens after an audio route disconnect')
assert(androidNative.includes('std::enable_shared_from_this<Engine>'),
  'Android keeps native callbacks alive while Oboe owns their stream')
assert(androidNative.includes('reportedSampleRate_.store(renderSampleRate_'),
  'Android refreshes the public sample rate after route recovery')
assert(/aarMetadata\s*\{[\s\S]*?minCompileSdk\s*=\s*35/.test(androidBuildGradle),
  'Android AAR remains consumable by API 35 projects')
assert(!androidThread.includes('private final int sampleRate;'),
  'Android does not cache a stale sample rate across route changes')
assert(androidNative.includes('constexpr double sineGain = 0.44'),
  'Android native renderer uses the tested conservative source calibration')
assert(androidNative.includes('std::max(1, voiceCount)'),
  'Android native renderer applies the tested event voice count')
assert(androidNative.includes('noiseIncrementDelta'),
  'Android renders variable-rate sample-and-hold noise')
assert(iosModule.includes('noisePhaseIncrDelta'),
  'iOS renders variable-rate sample-and-hold noise')
assert(androidNative.includes('filterAlpha'), 'Android has per-voice low-pass filtering')
assert(iosModule.includes('filterAlpha'), 'iOS has per-voice low-pass filtering')
assert(androidNative.includes('pulseWidthDelta'), 'Android sweeps pulse width')
assert(iosModule.includes('pulseWidthDelta'), 'iOS sweeps pulse width')
assert(androidNative.includes('wavetableSize'), 'Android renders custom wavetables')
assert(iosModule.includes('wavetableSize'), 'iOS renders custom wavetables')
assert(androidNative.includes('bitDepth'), 'Android applies optional bit-depth reduction')
assert(iosModule.includes('bitDepth'), 'iOS applies optional bit-depth reduction')
assert(iosModule.includes('SynthCommand chordCommands[ABSOLUTE_MAX_VOICES]'),
  'iOS prepares the complete chord before queue publication')
assert(iosModule.includes('atomic_store_explicit(&commandWriteIndex, write, memory_order_release)'),
  'iOS publishes a chord with one release-store')
assert(iosModule.includes('AVAudioSessionRouteChangeNotification'),
  'iOS observes hardware audio route changes')
assert(iosModule.includes('AVAudioEngineConfigurationChangeNotification'),
  'iOS observes audio-engine configuration changes')
assert(iosModule.includes('resumeAudioEngineAfterRouteChange'),
  'iOS resumes a stopped engine after an audio route change')
assert(exampleApp.indexOf("addEventListener('postlayout'") < exampleApp.indexOf('win.open()'),
  'example registers postlayout before opening')
assert(exampleApp.indexOf('function startAppCycles()') < exampleApp.lastIndexOf('Synth.startEngine('),
  'example starts the engine from its deferred app cycle')

for (const [name, source] of [['Android', androidNative], ['iOS', iosModule]]) {
  assert(source.includes('#include "../../shared/synth_core.h"'), `${name} renderer includes the shared DSP core`)
  for (const call of ['synth_envelope(', 'synth_filter_process(', 'synth_fm_sine(', 'synth_filter_set(']) {
    assert(source.includes(call), `${name} renderer uses ${call} from the shared DSP core`)
  }
}
assert.strictEqual(androidThread.match(/EXTRA_COUNT = (\d+)/)[1], androidNative.match(/kExtraCount = (\d+)/)[1],
  'Java and C++ agree on the number of extra voice parameters')
for (const key of ['decay', 'sustain', 'filterType', 'filterHz', 'filterHzEnd', 'filterQ', 'fmRatio', 'fmDepth', 'detune']) {
  assert(androidModule.includes(`defaults.put("${key}"`), `Android getDefaults reports ${key}`)
  assert(iosContract.includes(`@"${key}"`), `iOS getDefaults reports ${key}`)
}
assert(androidModule.includes('"filterTypes"') && iosContract.includes('@"filterTypes"'),
  'both ports report the filter types')
assert(androidModule.includes('fireEvent("patternend"') && iosModule.includes('@"patternend"'),
  'both ports fire patternend')

const noteValueList = values => [...values].sort().join(' ')
const documentedNoteValues = read('documentation/api.md')
  .split('\n\n')
  .find(paragraph => paragraph.includes('The allowed `noteValue` strings'))
const androidNoteValues = androidContract
  .slice(androidContract.indexOf('Validation optionalNoteValue('))
  .split('Validation validNote(')[0]
const iosNoteValues = iosContract
  .slice(iosContract.indexOf('OptionalNoteValue('))
  .split('stringByTrimmingCharactersInSet')[0]
assert(documentedNoteValues, 'api.md documents the allowed noteValue strings')
assert.strictEqual(
  noteValueList([...documentedNoteValues.matchAll(/`(\d+[nt]\.?)`/g)].map(match => match[1])),
  noteValueList([...androidNoteValues.matchAll(/"(\d+[nt]\.?)"\.equals/g)].map(match => match[1])),
  'api.md lists exactly the noteValue strings Android accepts')
assert.strictEqual(
  noteValueList([...iosNoteValues.matchAll(/@"(\d+[nt]\.?)"/g)].map(match => match[1])),
  noteValueList([...androidNoteValues.matchAll(/"(\d+[nt]\.?)"\.equals/g)].map(match => match[1])),
  'iOS accepts exactly the noteValue strings Android accepts')

console.log('api_contract_regression: all tests passed')
