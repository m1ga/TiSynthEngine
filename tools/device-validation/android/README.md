# Android physical callback telemetry

`instrument_android.py` adds diagnostics to an **external source snapshot**. It refuses the production repository and reuses the shared `instrument_stats.py` patch. Do not distribute its module archive as a production package.

```sh
python3 tools/device-validation/android/instrument_android.py /tmp/physical-module-snapshot
ti build -p android --build-only --project-dir /tmp/physical-module-snapshot/android
```

The snapshot-only `getPhysicalStats({freeze:true})` requests a freeze. Keep rendering and read `getPhysicalStats({freeze:false})` until `frozen` is true before accepting a final histogram. Reset/configuration changes create a new epoch without clearing large arrays in the callback.

Telemetry contains bounded 0.001-load bins, exact callback/deadline-miss counts, invalid values, overflow, and callback-end CPU-ID samples. The p95 upper bound is exclusive; overflow has no finite upper bound. CPU samples identify where the callback ended, without claiming affinity or where its entire work ran. Timing is the production render-work interval; the bounded telemetry publication follows it. Preserve this diagnostic overhead and distinction in reports.

Use a separate application ID and retain the exact native source/library hashes. Keep warmup/reset, the timed load, and producer/memory/recovery stress separate. Polling the production `renderLoad` value does not yield an all-callback p95. An accumulated production `peakRenderLoad` below a limit does establish that stronger bound for the recorded interval.

For attribution after a failed wall-time run, add `--thread-cpu` when instrumenting the snapshot. This records a second histogram using `CLOCK_THREAD_CPUTIME_ID` around the same callback work. The original wall endpoint is captured before the CPU end timestamp call; CPU duration includes timestamp-call overhead. Both histograms use the same reset/freeze epoch. CPU time measures scheduled execution time and still varies with CPU frequency. Treat this mode as a diagnostic, and retain its added overhead in the report.

`spatial_exact_reference.c` contains the independent shipping scalar FIR reference. Compile it with the Android NDK at `-O2`, push the executable to a temporary device directory, and run it without opening an audio stream. It compares output bits and full spatial state at 16, 44.1, 48, 96 and 192 kHz, including position ramps and retargets. Its success verifies arithmetic equivalence, not full-render deadline performance.

The isolated Titanium harness is `diagnostic-app.js`. Use its own app ID (`ti.synthengine.physicaldiagnostic`) and replace that app's `Resources/app.js` with this file. Install the instrumented snapshot module only into that app. Build/install using the project's normal Titanium Android flow, then run:

```sh
python3 tools/device-validation/android/run_phase.py feasibility 30000 --serial DEVICE --output /tmp/audio-feasibility
python3 tools/device-validation/android/run_phase.py controlled 600000 --serial DEVICE --output /tmp/audio-controlled
python3 tools/device-validation/android/run_phase.py memory 60000 --serial DEVICE --output /tmp/audio-memory --config '{"physicalTelemetry":false}'
```

Start the ten-minute phase only after feasibility is acceptable and the device is reserved. The driver force-stops only its explicitly selected diagnostic package, writes its config/results, and preserves other app data. It never changes device settings. It collects RSS/HWM, frequency residency and thermal snapshots, callback results, and app logs. The memory phase logs caller JS input separately, exercises all four maximum-size scores and full-score updates, then performs five engine recovery cycles. Its process-wide memory figures include Titanium/Java/bridge and caller-input allocations; report that scope separately from native accounting at 192 kHz.
