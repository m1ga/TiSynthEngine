const test = require('node:test')
const assert = require('node:assert/strict')
const create = require('../example/lib/sequenceOwner')
function fixture() {
  let current = 0, serial = 0, retired = false
  const listeners = new Set(), publications = [], stops = []
  const synth = {
    addEventListener: (_, fn) => listeners.add(fn), removeEventListener: (_, fn) => listeners.delete(fn),
    playSequence(payload) { if (current) return 0; publications.push(payload); current = ++serial; return current },
    stopSequence({ sequenceId }) { stops.push(sequenceId); return !retired && current === sequenceId },
    updateSequence: () => true,
    getSequenceState: () => retired ? null : { playing: true },
  }
  return { owner: create(synth), synth, publications, stops, listeners,
    retire() { retired = true },
    drain(reason = 'stopped') { const sequenceId = current; current = 0; retired = false; for (const fn of [...listeners]) fn({ sequenceId, reason }) },
  }
}
test('latest complete replacement waits for native retirement and merges pending voice edits', () => {
  const f = fixture()
  assert.equal(f.owner.submit({voice:{waveType:1},steps:[{notes:[60]}]}),true)
  f.owner.submit({voice:{waveType:2},steps:[{notes:[62]}]})
  f.owner.submit({voice:{waveType:3,filterType:'lowpass',filterHz:900},steps:[{notes:[64]}]})
  f.owner.update({voice:{volume:.2}})
  assert.equal(f.publications.length,1); assert.deepEqual(f.stops,[1])
  f.drain()
  assert.equal(f.owner.id(),2); assert.equal(f.publications.length,2)
  assert.deepEqual(f.publications[1].voice,{waveType:3,filterType:'lowpass',filterHz:900,volume:.2})
  assert.deepEqual(f.publications[1].steps,[{notes:[64]}])
  for (const fn of f.listeners) fn({sequenceId:1,reason:'completed'})
  assert.equal(f.owner.id(),2)
})
test('stop and reset cancel pending whole publications without restarting a retired owner', () => {
  const f=fixture();f.owner.submit({steps:[{notes:[60]}]});f.owner.submit({steps:[{notes:[62]}]})
  f.owner.stop();f.drain();assert.equal(f.publications.length,1)
  f.owner.submit({steps:[{notes:[64]}]});f.owner.submit({steps:[{notes:[65]}]})
  const late=[...f.listeners][0];f.owner.reset();late({sequenceId:2,reason:'stopped'})
  assert.equal(f.publications.length,2);assert.equal(f.listeners.size,0)
})
test('an already retired native ID still waits for its queued completion to release capacity', () => {
  const f=fixture();f.owner.submit({steps:[{notes:[60]}]});f.retire()
  assert.equal(f.owner.submit({steps:[{notes:[67]}]}),true)
  assert.equal(f.publications.length,1)
  f.drain('completed');assert.equal(f.publications.length,2)
})
test('a rejected complete publication reports failure instead of pretending it has an ID', () => {
  let reported=0
  const owner=create({addEventListener(){},removeEventListener(){},playSequence:()=>0},{rejected(){reported++}})
  assert.equal(owner.submit({steps:[{notes:[60]}]}),false);assert.equal(owner.id(),0);assert.equal(reported,1)
})

test('a complete pending score replaces obsolete source and filter fields', () => {
  const f=fixture();f.owner.submit({steps:[{notes:[60]}]})
  f.owner.submit({voice:{waveType:0,filterType:'lowpass',filterHz:900,fmRatio:2,fmDepth:2},steps:[{notes:[62]}]})
  const next={timeUnit:'beats',voice:{waveType:3,filterType:'none',volume:.2},steps:[{notes:[64]}]}
  f.owner.update(next);f.drain()
  assert.deepEqual(f.publications[1],next)
  assert.equal('filterHz' in f.publications[1].voice,false)
  assert.equal('fmRatio' in f.publications[1].voice,false)
  if(process.env.SYNTH_DEFERRED_SCORE) require('node:fs').writeFileSync(process.env.SYNTH_DEFERRED_SCORE,JSON.stringify(f.publications[1]))
})
