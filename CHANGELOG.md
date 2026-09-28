# Changelog

## [1.0.0] - Unreleased

First public release.

### Added

- Native low-latency tone synthesis for Titanium on Android and iOS.
- Eight public methods: `getDefaults`, `startEngine`, `playTone`, `playChord`,
  `playPattern`, `setVolume`, `stopAll` and `shutdown`.
- Sine, square, sawtooth, triangle, white-noise and custom wavetable sources.
- Envelopes, stereo pan movement, pitch sweeps, vibrato, chords and timed
  patterns with tempo, note values and swing.
- Variable-rate sample-and-hold noise with optional rate sweeps.
- A per-voice one-pole low-pass filter for noise and oscillator shaping.
- Pulse-width control and pulse-width sweeps for the square oscillator.
- Custom stepped wavetables with 2 to 64 normalized samples.
- Optional 2-bit to 16-bit amplitude quantization.
- Voice pools from 1 to 32 voices and patterns of up to 512 structured steps.
- `conservative`, `balanced`, `speakerSafe` and `raw` mixing profiles.
- `startEngine({ audioSession })` selects the iOS session category:
  `playback` (default) or `ambient`, which respects the silent switch and mixes
  with other apps' audio. The category is reapplied after a route change.
  Android validates the same values and leaves its output unchanged.
- Fixed event gain, atomic chord publication, smoothed master level and a
  stereo-linked lookahead limiter.
- Automatic stream recovery after output-route changes.
- Strict cross-platform validation. Unknown options use `unsupported_option`,
  and non-object pattern steps use `invalid_step_type`. Pattern envelopes are
  validated against each step's inherited timing.
- Host-side contract, parity and DSP regressions, an interactive example app,
  and complete API and sound-design documentation.
- Requires Titanium SDK 13.3.1.GA or newer and iOS 15 or newer.
