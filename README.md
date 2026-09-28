# TiSynthEngine

TiSynthEngine is a native synthesizer for Titanium apps on Android and iOS. It generates tones, chords, sweeps, noise and timed patterns without bundled audio files.

The public module id is `ti.synthengine` and the current API version is `1.0.0`.

## What it is for

TiSynthEngine works well for game and interface feedback, retro effects, alarms, notes, chords, arpeggios and simple noise-based percussion.

It is a synthesizer, not a sample player. It does not decode `.wav` or `.mp3` files, loop a voice indefinitely, or return handles for individual notes. See [known limits](documentation/recommendations.md#known-limits) before using it for a long-running music or ambience system.

## Requirements

- Titanium SDK 13.3.1.GA or newer.
- Android projects must compile against API 35 or newer.
- Android: `arm64-v8a`, `armeabi-v7a`, `x86` or `x86_64`.
- iOS 15.0 or newer.
- iOS builds can target a device, simulator or Mac Catalyst.

## Install

Copy these archives into the root of the Titanium app:

- `android/dist/ti.synthengine-android-1.0.0.zip`
- `ios/dist/ti.synthengine-iphone-1.0.0.zip`

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
  mixingProfile: 'conservative'
}) === false) {
  throw new Error('The audio engine could not start')
}

synth.playTone({ note: 'A4' })
```

Call `shutdown()` when the screen or app that owns the audio closes.

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

The module exposes exactly eight methods.

| Method                 | Returns   | What it does                                                                |
| ---------------------- | --------- | --------------------------------------------------------------------------- |
| `getDefaults()`        | `Object`  | Reports the API version, defaults, limits, profiles and waveform constants. |
| `startEngine(options)` | `Boolean` | Opens the native stream and fixes its voice capacity and mixing profile.    |
| `playTone(options)`    | `Boolean` | Plays one pitch with optional sweep, pan movement and vibrato.              |
| `playChord(options)`   | `Boolean` | Starts 1 to 32 pitches as one atomic event.                                 |
| `playPattern(options)` | `Boolean` | Schedules 1 to 512 structured note or rest steps.                           |
| `setVolume(options)`   | `Boolean` | Changes the master level with a short ramp.                                 |
| `stopAll()`            | nothing   | Releases all sounding voices through a short fade.                          |
| `shutdown()`           | nothing   | Stops the stream, cancels pending pattern steps and frees resources.        |

The five configurable calls return `true` when accepted. A `false` result means the payload was rejected, the engine is not running, the stream could not open, or the command queue was full.

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

| Constant   | Value | Character                                                 |
| ---------- | ----: | --------------------------------------------------------- |
| `SINE`     |   `0` | Pure fundamental; useful for clean pitches and low notes. |
| `SQUARE`   |   `1` | Hollow, bright and suited to retro sounds.                |
| `SAWTOOTH` |   `2` | Dense harmonic spectrum with a buzzy edge.                |
| `TRIANGLE` |   `3` | Softer than square, with more body than sine.             |
| `NOISE`    |   `4` | White or sample-and-hold noise for hits and texture.      |
| `WAVETABLE` |  `5` | A repeating stepped table supplied by the caller.         |

Square and sawtooth use PolyBLEP band-limiting. Noise is unpitched, so its `frequency` value does not change the sound. `WAVETABLE` requires `waveTable` with 2 to 64 values from -1 to 1.

## Wave-shaping controls

The shaping controls add variable-rate sample-and-hold noise, a per-voice low-pass filter, variable pulse width, custom wavetables and optional amplitude quantization. All of them are optional and disabled by default.

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

`maxVoices` is capacity, not a volume control. When the pool is full, the engine steals the voice with the least time remaining and gives it a short tail.

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

MIT. See [LICENSE](LICENSE).
