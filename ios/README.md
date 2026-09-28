# TiSynthEngine iOS module

This folder contains the iOS implementation of `ti.synthengine` 1.1.0. The
public API is documented in the root [README](../README.md) and the
[API reference](../documentation/api.md).

## Runtime

The renderer uses `AVAudioEngine` and `AVAudioSourceNode` at the active audio
session sample rate. It observes route and engine-configuration changes and
resumes the graph when a hardware transition stops it.

The native engine provides:

- 1 to 32 voices with short steal and stop tails;
- sine, PolyBLEP square, PolyBLEP sawtooth, triangle, noise and custom wavetable
  sources;
- variable-rate sample-and-hold noise, per-voice low-pass filtering, variable
  pulse width and optional amplitude quantization;
- fixed event gain and atomic chord publication;
- a lock-free render-thread consumer for the native command queue;
- smoothed master volume and a stereo-linked lookahead limiter;
- `conservative`, `balanced`, `speakerSafe` and `raw` mixing profiles.

The module targets iOS 15 or newer. Release builds use Titanium SDK
13.3.1.GA, which is also the minimum declared by the module manifest.
The archive supports device, simulator and Mac Catalyst builds.

Build the distributable module with the Titanium CLI from `ios/`. The expected
archive is `dist/ti.synthengine-iphone-1.1.0.zip`.
