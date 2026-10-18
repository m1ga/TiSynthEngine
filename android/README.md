# TiSynthEngine Android module

This folder contains the Android implementation of `ti.synthengine` 1.0.0. The public API is documented in the root [README](../README.md) and the [API reference](../documentation/api.md).

## Runtime

The renderer runs in C++ through Oboe's floating-point audio callback. It requests low-latency mode and exclusive sharing when the device supports them, then falls back to a compatible stream when needed. Output-route errors reopen the stream and refresh its sample rate.

The native engine provides:

- 1 to 32 voices with short steal and stop tails;
- sine, PolyBLEP square, PolyBLEP sawtooth, triangle, noise and custom wavetable sources;
- variable-rate sample-and-hold noise, per-voice low-pass filtering, variable pulse width and optional amplitude quantization;
- fixed event gain and atomic chord publication;
- a lock-free audio-thread consumer for the native command queue;
- smoothed master volume and a stereo-linked lookahead limiter;
- `conservative`, `balanced`, `speakerSafe` and `raw` mixing profiles.

`build.gradle` resolves Oboe 1.10.0 through Prefab and extends Titanium's generated `Android.mk`. Supported ABIs are `arm64-v8a`, `armeabi-v7a`, `x86` and `x86_64`. The packaged AAR requires compile API 35 or newer and can be used by Titanium 13.2.0 projects.

`src/ti/synthengine/SynthDsp.java` is the host-testable mixing and limiter reference. The runtime renderer is in `jni/SynthOboeEngine.cpp`.

## Host tests

Run from this directory:

```sh
test_dir=$(mktemp -d /tmp/ti-synth-test.XXXXXX)
javac -d "$test_dir" \
  src/ti/synthengine/SynthContract.java \
  test/ti/synthengine/SynthContractRegression.java \
  src/ti/synthengine/SynthDsp.java \
  test/ti/synthengine/SynthDspRegression.java
java -cp "$test_dir" ti.synthengine.SynthContractRegression
java -cp "$test_dir" ti.synthengine.SynthDspRegression
```

Build the distributable module with the Titanium CLI from `android/`. The expected archive is `dist/ti.synthengine-android-1.0.0.zip`.

Binaural HRTF data: copyright 1994 MIT Media Laboratory, measured by Bill Gardner and Keith Martin. Use is permitted with attribution. See [MIT KEMAR](https://sound.media.mit.edu/resources/KEMAR.html) and the module's third-party notices.
