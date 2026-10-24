'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../tools/device-validation/ios/bypass.js'), 'utf8');

function fixture(options = {}) {
  let clock = 0, serial = 0, voices = 0, started = false, resetAt = 0, freezeAt = null, report;
  const tasks = new Map(), events = {}, voiceIds = new Set();
  const schedule = (fn, delay, interval = 0) => { const id = ++serial; tasks.set(id, { fn, at: clock + delay, interval }); return id; };
  function stats() {
    const end = freezeAt === null ? clock : freezeAt;
    const rate = 48000;
    return {
      physicalPreferredSampleRateRequest: { requested: rate, accepted: !options.rejectRate },
      sampleRate: rate, callbackFrames: 1024, activeVoices: voices,
      physicalAudioSession: { sessionSampleRate: rate, sourceNodeSampleRate: rate, outputNodeSampleRate: rate,
        ioBufferDuration: 1024 / rate, systemOutputVolume: options.changeVolume && freezeAt !== null ? 0.5 : 1,
        ports: [{ name: 'Speaker', type: 'Speaker' }] },
      physicalTelemetry: { frozen: freezeAt !== null && clock > freezeAt,
        renderedFrames: options.stall ? 0 : Math.floor((end - resetAt) * rate / 1000 / 1024) * 1024 }
    };
  }
  const synth = {
    getDefaults: () => ({ waveTypes: { SINE: 0 } }),
    getStats: request => { if (request && request.physicalFreeze) freezeAt = clock; return stats(); },
    start: () => { started = true; return true; }, setVolume() {}, setStatsEnabled() {},
    noteOn: options => {
      assert.ok(Number.isInteger(options.voiceId) && options.voiceId > 0, 'Caller-owned voice ID is required');
      assert.ok(!voiceIds.has(options.voiceId), 'Voice IDs must not be reused');
      voiceIds.add(options.voiceId); voices++; return true;
    }, stopAll: () => { voices = 0; }, shutdown() {},
    resetStats: () => { resetAt = clock; freezeAt = null; }
  };
  const Ti = {
    App: { addEventListener: (name, fn) => { events[name] = fn; } },
    Filesystem: { getFile: () => ({ write: text => { report = JSON.parse(text); } }) },
    UI: { createLabel: () => ({}), createWindow: () => {
      let open;
      return { add() {}, addEventListener: (_, fn) => { open = fn; }, open: () => open() };
    } }
  };
  class VirtualDate extends Date { constructor() { super(clock); } static now() { return clock; } }
  vm.runInNewContext(source, { Ti, require: () => synth, Date: VirtualDate,
    setTimeout: (fn, ms) => schedule(fn, ms), setInterval: (fn, ms) => schedule(fn, ms, ms),
    clearInterval: id => tasks.delete(id) });
  function finish() {
    for (let i = 0; i < 1000 && !report.complete && !report.error; i++) {
      const [id, task] = [...tasks].sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0] || [];
      assert.ok(task, 'A pending run must have a timer');
      clock = task.at;
      if (task.interval) task.at += task.interval; else tasks.delete(id);
      task.fn();
    }
    assert.ok(report.complete || report.error, 'Run must terminate');
    return report;
  }
  return { finish, pause: () => events.pause(), report: () => report, started: () => started };
}

let run = fixture();
let report = run.finish();
assert.equal(report.complete, true);
assert.deepEqual(report.phases.map(p => p.voices), [0, 1, 32, 32]);
assert.ok(report.phases.every(p => p.measuredAudioSeconds >= 30 && p.final.physicalTelemetry.frozen));
run.pause();
assert.equal(run.report().error, undefined, 'Leaving a completed run must preserve its result');
run = fixture(); run.pause();
assert.equal(run.report().complete, false);
assert.match(run.report().error, /paused/);
run = fixture({ rejectRate: true });
assert.equal(run.started(), false);
assert.match(run.report().error, /preference/);
report = fixture({ stall: true }).finish();
assert.equal(report.complete, false);
assert.match(report.error, /clock stalled/);
report = fixture({ changeVolume: true }).finish();
assert.equal(report.complete, false);
assert.match(report.error, /Route, volume or buffer changed/);
console.log('Physical bypass native-frame duration, freeze, pause, rate, stall and volume guards PASS');
