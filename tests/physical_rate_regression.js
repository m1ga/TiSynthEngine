'use strict';
// A requested hardware rate must never turn a resampled run into acceptance.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../tools/device-validation/ios/app.js'), 'utf8');

function run(request, rates) {
  const timers = [], appEvents = {};
  let tick;
  let report, started = false, sequence = 0, reset = false;
  const synth = {
    getDefaults: () => ({ waveTypes: { SQUARE: 1 } }),
    getStats: () => started ? {
      sampleRate: rates.renderer, callbackFrames: rates.frames || 1024, activeVoices: 32,
      physicalTelemetry: { callbacks: 28125, frozen: true, renderedFrames: 28800000, p95Upper: 0.1, deadlineMisses: 0, invalidNegativeOrNaN: 0 },
      physicalAudioSession: { sessionSampleRate: rates.session,
        sourceNodeSampleRate: rates.source, outputNodeSampleRate: rates.output, systemOutputVolume: rates.volume || 1, ioBufferDuration: 1024 / 48000, ports: [{ type: "Speaker", name: "Speaker" }] }
    } : { physicalPreferredSampleRateRequest: request },
    start: () => { started = true; return true; },
    setVolume() {}, setStatsEnabled() {}, setTempo: () => true,
    setMasterProcessing: () => true, playSequence: () => ++sequence,
    getSequenceState: () => ({}), resetStats: () => { reset = true; },
    shutdown() {}, stopAll() {}
  };
  const events = {};
  const Ti = {
    App: { id: 'isolated.rate.test', addEventListener: (name, fn) => { appEvents[name] = fn; } },
    Platform: { model: 'fixture', version: 'fixture' },
    API: { info() {} },
    Filesystem: { getFile: () => ({ write: text => { report = JSON.parse(text); } }) },
    UI: { createLabel: () => ({}), createButton: () => ({ addEventListener() {} }), createWindow: () => ({
      add() {}, addEventListener: (event, callback) => { events[event] = callback; },
      open: () => events.open()
    }) }
  };
  const context = { Ti, require: () => synth, setTimeout: callback => timers.push(callback),
    setInterval: callback => { tick = callback; return 1; }, clearInterval() {}, Date };
  vm.runInNewContext(source, context);
  timers.shift()(); // Begin after baseline.
  if (timers.length) timers.shift()(); // Check actual formats after warmup.
  return { report, started, reset, context, latest: () => report, pause: () => appEvents.pause(),
    tick: () => tick(), timeout: () => timers.shift()() };
}

const matched = { renderer: 48000, session: 48000, source: 48000, output: 48000 };
const requested = { requested: 48000, accepted: true, error: null };
let result = run({ ...requested, accepted: false }, matched);
assert.equal(result.started, false);
assert.equal(result.report.stage, 'failed');
for (const format of Object.keys(matched)) {
  result = run(requested, { ...matched, [format]: 44100 });
  assert.equal(result.report.stage, 'failed', format);
  assert.equal(result.reset, false, format);
}
result = run(requested, matched);
assert.equal(result.report.stage, 'measuring');
assert.equal(result.reset, true);
result = run(undefined, { renderer: 44100, session: 44100, source: 44100, output: 44100 });
assert.equal(result.report.stage, 'measuring'); // Default protocol retains native rate.
console.log('Physical preferred-rate rejection, four actual formats and default-rate regression PASS');

for (const format of Object.keys(matched)) {
  const rates = { ...matched };
  const late = run(requested, rates);
  rates[format] = 44100;
  late.context.finishMeasurement(Date.now() - 600000);
  assert.equal(late.latest().stage, 'failed', 'Final ' + format + ' must be revalidated');
  assert.equal(late.latest().renderTimingPassed, false);
}
const changed = { ...matched };
const during = run(requested, changed);
changed.renderer = 44100;
during.tick();
assert.equal(during.latest().stage, 'failed', 'Observed rate change invalidates measurement');
const paused = run(requested, { ...matched });
paused.context.finishMeasurement(Date.now() - 600000);
paused.pause();
paused.timeout();
assert.equal(paused.latest().stage, 'failed', 'Deferred callback cannot overwrite interruption');
assert.equal(paused.latest().renderTimingPassed, false);
console.log('Physical final-format and latched-interruption regression PASS');
