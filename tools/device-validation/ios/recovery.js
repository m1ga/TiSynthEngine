/* Controlled software pause -> the unchanged production recovery helper. */
Ti.App.idleTimerDisabled = true
var synth = require('ti.synthengine')
var report = { phase: 'preparing', startedAt: new Date().toISOString(), cycles: [],
  description: 'Controlled AVAudioEngine pause followed by production resumeAudioEngineAfterRouteChange; no hardware route or OS setting change' }
var file = Ti.Filesystem.getFile(Ti.Filesystem.applicationDataDirectory, 'recovery-report.json')
var win = Ti.UI.createWindow({ backgroundColor: '#161616' })
var label = Ti.UI.createLabel({ color: '#ffffff', text: 'Controlled stream recovery', left: 30, right: 30 })
win.add(label)
function save (phase) { report.phase = phase; file.write(JSON.stringify(report)); Ti.API.info('SYNTH_RECOVERY ' + JSON.stringify(report)) }
function memory () { return synth.getStats({ physicalMemory: true }) }
win.addEventListener('open', function () { setTimeout(start, 3000) })
function start () {
  report.baseline = memory()
  report.startAccepted = synth.start({ maxVoices: 32, maxSequences: 4, audioSession: 'playback' })
  synth.setStatsEnabled(true)
  synth.setVolume({ volume: 0.15 })
  synth.setMasterProcessing({ chorus: { enabled: true }, delay: { enabled: true }, reverb: { enabled: true }, saturation: { enabled: true, drive: 2 } })
  var steps = []
  for (var i = 0; i < 512; i++) {
    var notes = []; for (var n = 0; n < 32; n++) notes.push(36 + n)
    steps.push({ notes: notes })
  }
  var voice = { volume: 0.0005, waveType: 1, filterType: 'lowpass', filterHz: 1200, spatialX: 0.6, spatialY: -0.4,
    modulations: [{ target: 'filterHz', rate: 2, depth: 0.3 }, { target: 'pulseWidth', rate: 3, depth: 0.3 }] }
  report.ids = []
  for (var slot = 0; slot < 4; slot++) report.ids.push(synth.playSequence({ loop: true, stepDuration: 1, voice: voice, steps: steps }))
  var updates = 0
  var prepare = setInterval(function () {
    report.ids.forEach(function (id) { synth.updateSequence({ sequenceId: id, steps: steps }) })
    if (++updates === 3) { clearInterval(prepare); setTimeout(recover, 2000) }
  }, 1000)
  save('load-preparing')
}
function recover () {
  var before = memory()
  var requested = synth.getStats({ physicalRecovery: true, physicalMemory: true })
  setTimeout(function () {
    var after = memory()
    var states = report.ids.map(function (id) { return synth.getSequenceState({ sequenceId: id }) })
    report.cycles.push({ before: before, requested: requested, after: after, states: states })
    save('recovering')
    if (report.cycles.length < 10) setTimeout(recover, 1000)
    else {
      report.recovered = report.cycles.every(function (cycle) {
        return cycle.requested.physicalRecoveryRequested && cycle.after.physicalEngineRunning && cycle.states.every(function (state) { return state && state.playing }) &&
          cycle.before.physicalTransportIdentity === cycle.after.physicalTransportIdentity && cycle.before.physicalEffectsAllocatedBytes === cycle.after.physicalEffectsAllocatedBytes
      })
      synth.stopAll()
      setTimeout(function () { synth.shutdown(); report.afterShutdown = memory(); label.text = 'Stream recovery completed'; save('completed') }, 2000)
    }
  }, 500)
}
win.open()
