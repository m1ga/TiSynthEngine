# TiSynthEngine documentation

TiSynthEngine 1.0.0 is a native tone synthesizer for Titanium on Android and
iOS. It plays generated tones, chords, noise and timed patterns through the
same JavaScript contract on both platforms.

Start with the [README](../README.md) if you have not installed the module yet.
It covers the lifecycle, the eight public methods, the `patternend` event and
the most common options.

## Guides

- [Guide](guide.md): installation, startup, cleanup and day-to-day use.
- [API reference](api.md): every public method, option, default and limit.
- [Sound design](sound-design.md): synthesis basics, filters, FM and recipes.
- [Examples](examples.md): complete snippets for tones, chords and patterns.
- [Audio engine](audio-engine.md): signal path, mixing and platform details.
- [Recommendations](recommendations.md): gain staging, lifecycle and testing.
- [Troubleshooting](troubleshooting.md): rejected calls, silence and glitches.
- [Third-party notices](third-party-notices.md): licenses of bundled dependencies.

The runnable Titanium app is in [`example/app.js`](../example/app.js). Host-side
contract and DSP regressions are in [`tests/`](../tests/) and
[`android/test/`](../android/test/). The DSP math both platforms share is in
[`shared/`](../shared/).
