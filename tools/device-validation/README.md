# Physical validation diagnostics

These tools run an isolated app without changing Studio's presets or preferences. Raw device reports belong outside the repository, for example `/tmp/tisynth-physical/ipad`.

`ios/app.js` runs the real module API with 32 shared square voices, four looping eight-note sequences, binaural FIR, two active LFOs and chorus/delay/reverb/saturation. Voice volume is 0.01 and master volume 0.15. A 15-second warmup precedes statistics reset and at least 600 seconds of counted audio callbacks. A later phase publishes four complete 512-step, 32-note scores repeatedly, then measures shutdown/restart. The final button plays a quiet native phrase for listening; audible acceptance remains a separate human decision.

The production API reports an accumulated render maximum but no callback percentile. If that maximum is below 0.5, it proves the stronger all-callback bound. Otherwise, use the diagnostics-only source copy below. Do not interpret once-per-second samples of the latest callback as a callback p95. iOS output underruns remain unknown (`null`).

## Bounded native telemetry in a temporary source copy

Copy `ios/`, `shared/`, README, license, documentation and example into a directory outside this repository. Exclude old build and dist output. Then:

```sh
python3 tools/device-validation/instrument_stats.py /tmp/tisynth-physical/native-snapshot
clang -std=c11 -O2 -Wall -Wextra \
  -I/tmp/tisynth-physical/native-snapshot/shared \
  tools/device-validation/physical_stats_test.c -lm -o /tmp/physical-stats-test
/tmp/physical-stats-test
```

The script refuses to patch the production repository. `SYNTH_PHYSICAL_STATS` adds 2,048 fixed, lock-free epoch/count buckets. Reset advances the epoch in constant time. Counts and rendered-frame totals use 32-bit payloads per epoch, sufficient for the controlled 600-second protocol at every supported rate (at most 115.2 million frames at 192 kHz). Callback work has no allocation, lock, wait or bucket scan. Buckets have load width 0.001; bucket 2,047 includes overflow. Negative/NaN loads have a separate diagnostic counter. Deadline misses count render duration strictly greater than the block deadline. P95 uses nearest-rank `ceil(0.95 * count)` and an exclusive upper bound `(bucket + 1) / 1000`. Overflow has no finite upper bound.

The private iOS copy adds `physicalTelemetry` to its existing statistics dictionary. `getStats({ physicalFreeze: true })` requests freeze; a later callback acknowledges it, after which the histogram is stable. Full histogram export occurs only when frozen. The runner keeps scalar observations during the load and stores the histogram once, avoiding a large retained telemetry array.

`getStats({ physicalMemory: true })` additionally reads `TASK_VM_INFO` on the control thread: current/peak physical footprint, resident size/peak, native module object size and thermal state. The same private query records AVAudioSession sample rate, I/O buffer duration, output latency/route, and engine source/output format rates. Report these alongside the public renderer rate; a source format can be resampled to the hardware rate. It does not request a preferred rate or buffer. Baseline is after requiring the module and before starting audio. Report that scope explicitly: module object allocation already exists at baseline; its size is separately recorded. Footprint growth includes the bridge, runtime caches, diagnostic report and producer peaks, so it is not a pure DSP-allocation measurement. The 192 kHz native host accounting is separate from a device's actual sample rate.

For iOS memory, retain both charged physical footprint and resident high-water. [Apple's game-memory guidance](https://developer.apple.com/videos/play/wwdc2022/10106/) distinguishes the footprint from resident size, which also includes clean executable and mapped pages. A native-buffer/charged-footprint projection below a budget is not a whole-process RSS bound. A 192 kHz projection is explicitly an estimate when the device actually rendered at a lower rate.

Build the temporary iOS module, copy its framework into a newly created Classic app with a separate bundle ID, copy `ios/app.js` to `Resources/app.js`, and declare `ti.synthengine` 1.0.0 in that app's `tiapp.xml`. Sign with an existing valid development profile for the connected device. The diagnostic framework and package must never replace release/distribution artifacts.

The `physical_stats_benchmark.c` microbenchmark compares statistics publication overhead on a host. It does not measure device render overhead or establish device audio deadlines.

## Controlled recovery and listening

`ios/recovery.js` is a separate short runner. Its private option pauses AVAudioEngine and invokes the unchanged production `resumeAudioEngineAfterRouteChange` helper. It checks ten resumptions, retained transport/effect allocation, active sequence states and process high-water footprint. This is controlled software stream recovery; it does not simulate a hardware route change. It writes `recovery-report.json` without overwriting the completed ten-minute report.

For a preview without rerunning the stress protocol, copy `ios/listen.js` to the isolated app's `Resources/app.js`, rebuild/install the same isolated app with the final production module, then launch it. Nothing plays automatically. César can tap **Play quiet native phrase** and **Stop audio**. This demonstrates a native phrase, overlapping release and a selective reverb tail; complete game, spatial and preset listening acceptance remains separate. No Studio preferences or library are changed.
