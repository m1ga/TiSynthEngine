# TiSynthEngine

TiSynthEngine is a native synthesizer for Titanium apps on Android and iOS. It generates tones, chords, sweeps, noise and timed patterns without bundled audio files. Optional `spatialX` and `spatialY` provide headphone positioning on a front plane; see [binaural positioning](documentation/api.md#binaural-front-plane-positioning) and [app integration](documentation/guide.md#integrate-binaural-in-a-2d-app).

The module id is `ti.synthengine`. API version `1.0.0` is an unreleased beta for the first public release.

## What it is for

Use TiSynthEngine for game and interface feedback, retro effects, alarms, notes, chords, arpeggios and simple noise-based percussion. Decay and sustain, a resonant filter, FM and detune also produce plucks, keys, bells and thick pads.

It is a synthesizer, not a sample player. It does not decode `.wav` or `.mp3` files, play recorded samples or provide MIDI. Sustained voices use app-supplied IDs for release and live updates. See [known limits](documentation/recommendations.md#known-limits) before using it for a long-running music or ambience system.

## Requirements

- Titanium SDK 13.2.0.GA or newer.
- Android projects must compile against API 35 or newer.
- Android: `arm64-v8a`, `armeabi-v7a`, `x86` or `x86_64`.
- iOS 15.0 or newer.
- iOS builds can target a device, simulator or Mac Catalyst.

## Install

For the unreleased beta, use both packages built from the current source in `android/dist` and `ios/dist`, then copy them into the Titanium app root. The [latest release](https://github.com/macCesar/TiSynthEngine/releases/latest) is a separate snapshot; copying new source does not update its packages:

- `ti.synthengine-android-1.0.0.zip`
- `ti.synthengine-iphone-1.0.0.zip`

Declare both platform modules in `tiapp.xml`:

```xml
<modules>
  <module platform="android" version="1.0.0">ti.synthengine</module>
  <module platform="iphone" version="1.0.0">ti.synthengine</module>
</modules>
```

The Titanium CLI will install them under `modules/<platform>/ti.synthengine/1.0.0/` during the next build.

## Quick start

```javascript
var synth = require('ti.synthengine')
var contract = synth.getDefaults()

if (contract.apiVersion !== '1.0.0') {
  throw new Error('TiSynthEngine 1.0.0 is required')
}

if (synth.startEngine({
  maxVoices: 16,
  mixingProfile: 'conservative',
  audioSession: 'ambient'
}) === false) {
  throw new Error('The audio engine could not start')
}

synth.playTone({ note: 'A4' })
```

This game-oriented example uses `ambient`: on iOS it respects silent mode and mixes with other apps. The module default is `playback`, which ignores silent mode. Android validates this option without changing its output. Changing the session policy requires `shutdown()` followed by `startEngine()`.

Call `shutdown()` when the screen or app that owns the audio closes. To try every public method, follow the [interactive example setup and test checklist](example/README.md).

## Start after layout

Do not open the low-latency stream while Titanium is still laying out the first window. On some Android devices that work competes with audio startup and the first sound underruns.

Register `postlayout` before opening the window, debounce it, and cancel the pending start during cleanup. A plain screen usually settles within 250 ms:

```javascript
var engineRunning = false
var startTimer = null

function onPostLayout () {
  if (startTimer !== null) clearTimeout(startTimer)
  startTimer = setTimeout(function () {
    startTimer = null
    win.removeEventListener('postlayout', onPostLayout)
    engineRunning = synth.startEngine({ maxVoices: 16 })
  }, 250)
}

function cleanup () {
  win.removeEventListener('postlayout', onPostLayout)
  if (startTimer !== null) clearTimeout(startTimer)
  if (engineRunning) synth.shutdown()
}

win.addEventListener('postlayout', onPostLayout)
win.addEventListener('close', cleanup)
win.open()
```

Apps that start ads, remote configuration or entry animations may need a longer quiet period. The [guide](documentation/guide.md#lifecycle) and [`example/app.js`](example/app.js) include full cleanup.

## Public methods

Native sample-clock sequencing, stereo chorus/delay/reverb and optional creative saturation are available through capability-detected beta APIs. New processing starts disabled. See the [transport and processing contract](documentation/api.md#native-sequence-transport).

The module exposes nineteen methods and one event, `patternend`.

| Method                  | Returns   | What it does                                                                                             |
| ----------------------- | --------- | -------------------------------------------------------------------------------------------------------- |
| `getStats()`            | `Object`  | Returns basic counters and enabled diagnostics; see the [stats contract](documentation/api.md#getstats). |
| `resetStats()`          | nothing   | Resets accumulated statistics without stopping sounds.                                                   |
| `setStatsEnabled(bool)` | `Boolean` | Enables optional timing and signal measurements (off by default).                                        |
| `getDefaults()`         | `Object`  | Reports the API version, defaults, limits, profiles and waveform constants.                              |
| `startEngine(options)`  | `Boolean` | Opens the native stream and fixes its voice capacity and mixing profile.                                 |
| `noteOn(options)`      | `Boolean` | Starts an identified sustained voice; see [sustained notes](documentation/api.md#noteonoptions). |
| `noteOff(options)`     | `Boolean` | Begins the identified voice’s release from its current level. |
| `updateVoice(options)` | `Boolean` | Ramps compatible active-voice parameters over 10 ms without retriggering. |
| `playTone(options)`     | `Boolean` | Plays one pitch with optional sweep, pan movement and vibrato.                                           |
| `playChord(options)`    | `Boolean` | Starts 1 to 32 pitches as one atomic event.                                                              |
| `playPattern(options)`  | `Boolean` | Schedules 1 to 512 structured note or rest steps.                                                        |
| `playSequence(options)` | `Boolean` | Starts sample-clock sixteenth-note transport with step chords. |
| `updateSequence(changes)` | `Boolean` | Publishes an atomic transport edit without restarting. |
| `stopSequence()` | `Boolean` | Stops transport and releases only its voices. |
| `getSequenceState()` | `Object` | Reports applied step, musical position, tempo and revision. |
| `setMasterProcessing(options)` | `Boolean` | Configures fixed input gain, stereo effects and optional saturation. |
| `setVolume(options)`    | `Boolean` | Changes the master level with a short ramp.                                                              |
| `stopAll()`             | nothing   | Releases all sounding voices through a short fade.                                                       |
| `shutdown()`            | nothing   | Stops the stream, cancels pending pattern steps and frees resources.                                     |

The thirteen Boolean methods return `true` when accepted. A `false` result means the payload was rejected, the engine is not running, the stream could not open, or the command queue was full.

## Strict options

Option dictionaries are closed contracts. A misspelled or unsupported key is rejected with `reason=unsupported_option`. Values with the wrong type, `NaN`, `Infinity` or an out-of-range number are also rejected. Explicit values are never clamped.

```javascript
synth.playTone({ note: 'C4', duraton: 200 })
// false; "duraton" is not a supported option
```

Each distinct validation failure writes one structured warning to logcat or the Xcode console:

```text
event=validation_warning method=playTone key=duraton reason=unsupported_option action=rejected value=200
```

Warnings are deduplicated by method, key and reason so a bad call in a loop does not flood the log. The [API reference](documentation/api.md) lists every accepted option and rejection reason.

## Notes and frequencies

`playTone` accepts either `note` or `frequency`. Do not send both.

```javascript
synth.playTone({ note: 'F#4' })
synth.playTone({ note: 'Sib3' })
synth.playTone({ frequency: 523.25 })
```

Note names accept English letters and solfège names. Accidentals use `#` or a lowercase `b`. Octave 4 is assumed when the octave is omitted. `A4` is 440 Hz and the other notes use twelve-tone equal temperament.

The frequency range starts at 1 Hz. Its upper bound is 45 percent of the current sample rate. Read `getDefaults().limits.frequency.max` after starting the engine if the app validates user input.

## Waveforms

Use the constants returned by `getDefaults()` instead of duplicating numbers:

```javascript
var WAVE = synth.getDefaults().waveTypes

synth.playTone({ note: 'C4', waveType: WAVE.SINE })
synth.playTone({ note: 'C4', waveType: WAVE.SQUARE })
synth.playTone({ note: 'C4', waveType: WAVE.SAWTOOTH })
synth.playTone({ note: 'C4', waveType: WAVE.TRIANGLE })
synth.playTone({
  note: 'C4',
  waveType: WAVE.WAVETABLE,
  waveTable: [-1, -0.4, 0.2, 1, 0.5, -0.2]
})
synth.playTone({
  frequency: 200,
  waveType: WAVE.NOISE,
  duration: 90,
  attack: 1,
  release: 75
})
```

| Constant    | Value | Character                                                 |
| ----------- | ----: | --------------------------------------------------------- |
| `SINE`      |   `0` | Pure fundamental; useful for clean pitches and low notes. |
| `SQUARE`    |   `1` | Hollow, bright and suited to retro sounds.                |
| `SAWTOOTH`  |   `2` | Dense harmonic spectrum with a buzzy edge.                |
| `TRIANGLE`  |   `3` | Softer than square, with more body than sine.             |
| `NOISE`     |   `4` | White or sample-and-hold noise for hits and texture.      |
| `WAVETABLE` |   `5` | A repeating stepped table supplied by the caller.         |

Square and sawtooth use PolyBLEP band-limiting. Noise is unpitched, so its `frequency` value does not change the sound. `WAVETABLE` requires `waveTable` with 2 to 64 values from -1 to 1.

## Wave-shaping controls

Optional shaping controls include variable-rate sample-and-hold noise, a per-voice low-pass filter, variable pulse width, custom wavetables and amplitude quantization. These controls are disabled by default.

```javascript
synth.playTone({
  frequency: 120,
  duration: 700,
  attack: 1,
  release: 500,
  waveType: WAVE.NOISE,
  noiseRate: 12000,
  noiseRateEnd: 700,
  lowPassHz: 2800,
  bitDepth: 8
})
```

`noiseRate` changes white noise into sample-and-hold noise at the requested update rate. `noiseRateEnd` sweeps that rate over the event. Zero keeps the original white-noise behavior or disables the sweep.

`lowPassHz` enables a per-voice one-pole low-pass filter. `bitDepth` quantizes amplitude from 2 to 16 bits; zero leaves the signal unquantized.

Square waves also accept `pulseWidth` from 0.05 to 0.95 and an optional `pulseWidthEnd` sweep:

```javascript
synth.playTone({
  note: 'A3',
  duration: 650,
  waveType: WAVE.SQUARE,
  pulseWidth: 0.12,
  pulseWidthEnd: 0.82,
  release: 180
})
```

These controls are available on `playTone`, at the root and step level of `playPattern`, and as fixed values on `playChord`. Chords do not accept sweep fields.

## Envelopes

Every note has an attack, an implicit full-level middle section and a release. `duration` includes all three.

```javascript
synth.playTone({
  note: 'C4',
  duration: 1000,
  attack: 200,
  release: 400
})
```

That sound rises for 200 ms, holds for 400 ms and fades for 400 ms.

Explicit envelopes must fit. `attack` must be shorter than `duration`, and `release` cannot exceed the time left after attack. Defaults are shortened when a very short duration needs it, but values supplied by the caller are rejected instead of rewritten.

Add `decay` and `sustain` for sounds that start loud and settle, such as plucks, keys and bells. The level falls from full scale to `sustain` over `decay` milliseconds, then holds until the release:

```javascript
synth.playTone({ note: 'E4', duration: 900, attack: 2, decay: 260, sustain: 0.18, release: 380 })
```

`sustain` below 1 needs a `decay`, and `attack` plus `decay` must fit in `duration`.

## Panning, sweeps and vibrato

`pan` runs from `-1` on the left to `1` on the right. `panEnd` moves the note across the stereo field during its lifetime:

```javascript
synth.playTone({
  frequency: 440,
  duration: 1800,
  pan: -1,
  panEnd: 1
})
```

The same event can sweep pitch and add vibrato. `lfoFreq` is the vibrato rate and `lfoDepth` is its pitch deviation in Hz:

```javascript
synth.playTone({
  frequency: 180,
  frequencyEnd: 1100,
  duration: 420,
  waveType: WAVE.SAWTOOTH,
  lfoFreq: 5,
  lfoDepth: 8
})
```

The [sound design guide](documentation/sound-design.md) explains how these controls change the result and includes tested recipes.

## Filter, FM and detune

`filterType` adds a resonant 12 dB per octave filter: `lowpass`, `highpass` or `bandpass`. `filterQ` sets the resonance and `filterHzEnd` sweeps the cutoff exponentially:

```javascript
synth.playTone({
  note: 'A2', duration: 1000, waveType: WAVE.SAWTOOTH,
  filterType: 'bandpass', filterHz: 350, filterHzEnd: 2400, filterQ: 5
})
```

`fmDepth` turns a sine into a two-operator FM voice. `fmRatio` is the modulator frequency as a multiple of the carrier, and `fmDepthEnd` moves the index over the note, so bells and electric pianos start bright and mellow out:

```javascript
synth.playTone({
  note: 'C5', duration: 2600, decay: 2000, sustain: 0.02,
  fmRatio: 3.5, fmDepth: 4, fmDepthEnd: 0
})
```

`detune` shifts a pitch in cents. A chord takes `detunes`, one offset per note, so repeated pitches beat and thicken:

```javascript
synth.playChord({
  notes: ['A3', 'A3', 'E4', 'E4'], detunes: [-8, 8, -8, 8],
  duration: 2000, waveType: WAVE.SAWTOOTH
})
```

Tones and pattern steps take every one of these keys. Chords take them as fixed values, use `detunes` instead of `detune`, and have no `*End` sweeps. The [API reference](documentation/api.md#expressive-controls) lists the ranges and the rules that tie the keys together.

## Chords and polyphony

Use `playChord` when notes belong to one simultaneous musical or sound-design event:

```javascript
synth.playChord({
  notes: ['C3', 'G3', 'C4', 'E4'],
  duration: 2600,
  attack: 500,
  release: 1000,
  waveType: WAVE.TRIANGLE,
  volume: 0.55,
  pans: [-0.7, -0.2, 0.2, 0.7]
})
```

The command is atomic. The render thread sees the complete chord or none of it, even if the queue is nearly full. The mixer also knows the chord's voice count and applies constant-power gain.

Separate `playTone` calls are independent events. They are appropriate for unrelated touches or collisions, but several of them can produce a hotter sum than the same notes in one chord.

`maxVoices` sets voice capacity without changing volume. When the pool is full, the engine steals the voice with the least time remaining and gives it a short tail.

## Patterns

Each pattern step is an object with exactly one pitch or a rest:

```javascript
synth.playPattern({
  bpm: 112,
  swing: 0.12,
  noteValue: '8n',
  waveType: WAVE.SQUARE,
  volume: 0.55,
  steps: [
    { note: 'C4' },
    { note: 'E4', pan: -0.4, panEnd: 0.4 },
    { rest: true },
    { note: 'G4', noteValue: '4n', lfoFreq: 6, lfoDepth: 10 }
  ]
})
```

Bare strings and numbers are rejected with `invalid_step_type`. Pattern-level options are inherited by steps, and a step can override its timing, envelope, level, waveform, pan, sweep or vibrato.

Step length uses step `duration`, step `noteValue`, pattern `noteValue`, pattern `stepDuration`, then the 100 ms default, in that order.

Only one pattern can be pending. Starting a new pattern cancels the future steps of the old one. `stopAll()` releases the notes already sounding but does not cancel future steps. Start a one-step rest or call `shutdown()` to cancel them.

An accepted pattern reports `patternend` unless `shutdown()` cancels it first. `completed` is `true` at the natural end and `false` when a newer pattern replaces it. Calling `stopAll()` leaves future steps scheduled and emits no event; `shutdown()` cancels them without an event. Sound may remain after the scheduler finishes. Remove the listener when the owner closes:

```javascript
function onPatternEnd (event) {
  if (event.completed) playNextPattern()
}

synth.addEventListener('patternend', onPatternEnd)
```

## Binaural headphone positioning

Send `spatialX` (left/right) and `spatialY` (below/above), each from -1 to 1, on a tone, chord or pattern. Either coordinate enables binaural; omit both for ordinary stereo. Never combine them with `pan`, `panEnd`, `pans` or `panSpread`, including inherited pattern fields. Positions stay fixed for each event.

Use stereo headphones and broadband noise to compare height. This is a front plane with two-channel output, without rear sources or head tracking. The [integration guide](documentation/guide.md#integrate-binaural-in-a-2d-app) covers app switches and coordinate conversion; the [interactive example](example/README.md#headphone-positioning) offers nine positions and a stereo comparison.

## Engine statistics

`getStats()` reads counters without resetting them. `setStatsEnabled(true)` enables optional callback timing and signal meters; `resetStats()` clears accumulated statistics without stopping playback. Diagnostics default off. Display `null` as unavailable or unmeasured so it remains distinct from zero.

See the [integration and cleanup recipe](documentation/guide.md#read-engine-statistics) and [statistics field reference](documentation/api.md#getstats). Render load measures callback work; it does not measure total app CPU.

## Mixing profiles

The profile is chosen in `startEngine()` and stays fixed until `shutdown()`.

| Profile        | Sine voice gain | Limiter ceiling | Good fit                                                        |
| -------------- | --------------: | --------------: | --------------------------------------------------------------- |
| `conservative` |           0.440 |            0.85 | General use and polyphony.                                      |
| `balanced`     |           0.622 |            0.85 | Mostly one or two voices when more level is needed.             |
| `speakerSafe`  |           0.311 |            0.70 | Small speakers, low pitches or device-side acoustic distortion. |
| `raw`          |           1.000 |            0.85 | App-managed gain staging and controlled tests.                  |

Protected profiles calibrate waveform RMS and divide chord gain by the square root of its voice count. A stereo-linked 10 ms lookahead limiter catches peaks that still exceed the ceiling. It is transparent below that threshold.

Start with `conservative`. If a small phone speaker sounds harsh while headphones remain clean, try `speakerSafe` and raise low pitches before adding more limiting.

## Master volume

`setVolume({ volume })` scales the whole mix and ramps changes over about 10 ms. Values up to 2 are accepted, though values above 1 make limiting more likely.

## Audio route changes

Connecting or disconnecting an output may cut the note crossing the hardware transition. Android reopens its Oboe stream and iOS resumes `AVAudioEngine`; later notes continue on the new route. See [troubleshooting](documentation/troubleshooting.md#audio-changes-after-connecting-headphones) if later notes remain silent.

## More documentation

- [Getting started guide](documentation/guide.md)
- [API reference](documentation/api.md)
- [Sound design guide](documentation/sound-design.md)
- [Copyable examples](documentation/examples.md)
- [Audio engine internals](documentation/audio-engine.md)
- [Recommendations](documentation/recommendations.md)
- [Troubleshooting](documentation/troubleshooting.md)

## Build and test

The [Android notes](android/README.md), [iOS notes](ios/README.md) and [audio engine guide](documentation/audio-engine.md#regression-coverage) contain the build and host-test commands.

## License

MIT. See [LICENSE](LICENSE). The Android module bundles Oboe under the Apache License 2.0; see [third-party notices](documentation/third-party-notices.md).
