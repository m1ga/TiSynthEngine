# Changelog

## [1.0.0] - Unreleased

Beta for the planned first public release.

### Added

- Add sample-clock `playSequence`, `updateSequence`, `stopSequence` and `getSequenceState`, triple-bank publication, step chords and queue-independent musical gates.
- Add shared stereo chorus, ping-pong delay, Schroeder reverb, explicit creative saturation and fixed input gain through `setMasterProcessing`. Effects default to bypass; tails drain on disable and panic invalidates histories without scanning buffers.
- Add two optional phase-preserving native LFOs on sustained/sequence voices, with filter, PWM and FM targets and free/tempo-sync rates.
- Add accumulated `preLimiterPeakDb` and `maxLimiterReductionDb` diagnostics. Studio preset calibration and listening acceptance are reported separately from module capabilities.

- Add matching `noteOn`, `noteOff` and `updateVoice` APIs for native sustained notes, current-level release and 10 ms live parameter ramps. Preserve timed calls and beta 1.0.0; detect methods by capability.

- Complete the interactive example with nine headphone positions, stereo comparison, spatial chords/patterns, audio-session selection and statistics read/reset/diagnostic controls.
- Handle playback rejection consistently in example actions and respect window safe areas; document setup, device checks, scheduler completion exceptions, statistics cleanup and idle render load.

- Add `getStats()`, `resetStats()` and opt-in `setStatsEnabled(Boolean)` with cross-platform voice, command and rejection counters, render timing and final-output signal meters.
- Avoid scanning empty voice/tail groups during render; verify identical host output against the previous engine.

- Add optional front-plane headphone binaural positioning with `spatialX` and `spatialY` on tones, chords and patterns.
- Share a compact MIT KEMAR HRTF FIR implementation between Android and iOS, including filter history during voice replacement and stopping.
- Add spatial contract parity and DSP regression coverage; ordinary calls keep stereo panning.
- Document per-effect JavaScript integration, app controls, installed-feature detection and stereo fallback; check API table coverage against the native contract.

- Native low-latency tone synthesis for Titanium on Android and iOS.
- Nineteen public methods, including native transport and processing in addition to: `getDefaults`, `startEngine`, `playTone`, `playChord`, `playPattern`, `noteOn`, `noteOff`, `updateVoice`, `setVolume`, `stopAll`, `shutdown`, `getStats`, `resetStats` and `setStatsEnabled`.
- Sine, square, sawtooth, triangle, white-noise and custom wavetable sources.
- Envelopes, stereo pan movement, pitch sweeps, vibrato, chords and timed patterns with tempo, note values and swing.
- Variable-rate sample-and-hold noise with optional rate sweeps.
- A per-voice one-pole low-pass filter for noise and oscillator shaping.
- Pulse-width control and pulse-width sweeps for the square oscillator.
- Custom stepped wavetables with 2 to 64 normalized samples.
- Optional 2-bit to 16-bit amplitude quantization.
- Voice pools from 1 to 32 voices and patterns of up to 512 structured steps.
- `conservative`, `balanced`, `speakerSafe` and `raw` mixing profiles.
- `startEngine({ audioSession })` selects the iOS session category: `playback` (default) or `ambient`, which respects the silent switch and mixes with other apps' audio. The category is reapplied after a route change. Android validates the same values and leaves its output unchanged.
- Fixed event gain, atomic chord publication, smoothed master level and a stereo-linked lookahead limiter.
- Automatic stream recovery after output-route changes.
- Strict cross-platform validation. Unknown options use `unsupported_option`, and non-object pattern steps use `invalid_step_type`. Pattern envelopes are validated against each step's inherited timing.
- Host-side contract, parity and DSP regressions, an interactive example app, and complete API and sound-design documentation.
- `decay` and `sustain` extend the envelope with a decay stage that settles to a held level before the release. The release scales the level it starts from, so one that begins during the decay stays continuous.
- A resonant state-variable filter: `filterType` (`lowpass`, `highpass` or `bandpass`), `filterHz`, `filterQ` and an exponential cutoff sweep with `filterHzEnd`. It runs after the one-pole `lowPassHz` filter.
- Two-operator FM on `SINE` voices: `fmRatio`, `fmDepth` and `fmDepthEnd`. The modulator keeps its ratio through pitch sweeps and vibrato.
- `detune` in cents on tones and pattern steps, and `detunes` for per-note offsets in a chord.
- The `patternend` event reports the end of every accepted pattern, with `completed: false` when a newer pattern replaces it.
- Cross-key validation with the reasons `sustain_without_decay`, `decay_exceeds_remaining_duration`, `filter_type_required`, `fm_requires_sine` and `fm_depth_required`.
- `getDefaults()` reports the new defaults and limits and a `filterTypes` list.
- The envelope, filter, sweep, FM and cents math live once in `shared/synth_core.h`, which both renderers include. Host checks cover that core, the Android engine render path and a replayed contract corpus for the iOS contract.
- Requires Titanium SDK 13.2.0.GA or newer and iOS 15 or newer.

### Fixed

- Protect sustained attack and release edges shorter than 3 ms from hard gating on Android and iOS. Longer envelopes and existing timed envelopes retain their behavior.
- Make the Android native limiter reach its target within the lookahead deadline, matching iOS and the Java DSP reference. Dense sums now respect the configured ceiling instead of escaping into hard clipping; drained-window gain resets without changing below-ceiling source gain.
