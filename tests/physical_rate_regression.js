'use strict';
// A requested hardware rate must never turn a resampled run into acceptance.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../tools/device-validation/ios/app.js'), 'utf8');

function run(request, rates) {
  const timers = [];
  let report, started = false, sequence = 0, reset = false;
  const synth = {
    getDefaults: () => ({ waveTypes: { SQUARE: 1 } }),
    getStats: () => started ? {
      sampleRate: rates.renderer, activeVoices: 32,
      physicalTelemetry: { callbacks: 100 },
      physicalAudioSession: { sessionSampleRate: rates.session,
        sourceNodeSampleRate: rates.source, outputNodeSampleRate: rates.output }
    } : { physicalPreferredSampleRateRequest: request },
    start: () => { started = true; return true; },
    setVolume() {}, setStatsEnabled() {}, setTempo: () => true,
    setMasterProcessing: () => true, playSequence: () => ++sequence,
    getSequenceState: () => ({}), resetStats: () => { reset = true; },
    shutdown() {}
  };
  const events = {};
  const Ti = {
    App: { id: 'isolated.rate.test', addEventListener() {} },
    Platform: { model: 'fixture', version: 'fixture' },
    API: { info() {} },
    Filesystem: { getFile: () => ({ write: text => { report = JSON.parse(text); } }) },
    UI: { createLabel: () => ({}), createButton: () => ({ addEventListener() {} }), createWindow: () => ({
      add() {}, addEventListener: (event, callback) => { events[event] = callback; },
      open: () => events.open()
    }) }
  };
  vm.runInNewContext(source, { Ti, require: () => synth,
    setTimeout: callback => timers.push(callback), setInterval() {}, clearInterval() {}, Date });
  timers.shift()(); // Begin after baseline.
  if (timers.length) timers.shift()(); // Check actual formats after warmup.
  return { report, started, reset };
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
