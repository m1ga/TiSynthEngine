/* Whole-process memory with full DSP, 5 Hz stats and a bounded report. */
Ti.App.idleTimerDisabled = true
var synth = require('ti.synthengine')
var file = Ti.Filesystem.getFile(Ti.Filesystem.applicationDataDirectory, 'memory-report.json')
var report = { schemaVersion: 1, complete: false, stages: [], editsAccepted: 0, editsRejected: 0,
  protocol: { voices: 32, sequences: 4, binaural: true, lfos: 2, effects: true,
    statsPollingHz: 5, pressureRounds: 30, pressureIntervalMs: 1000,
    reportScope: 'Whole process including caller score and bridge; compact snapshots, no retained per-poll history or full-report console logging' } }
var win = Ti.UI.createWindow({ backgroundColor: '#161616' })
var label = Ti.UI.createLabel({ color: '#ffffff', text: 'Physical memory check', left: 30, right: 30 })
win.add(label)
var poll, pressure, ids = [], invalid = false
function save () { file.write(JSON.stringify(report)) }
function snapshot (name) {
  var stats = synth.getStats({ physicalMemory: true })
  report.stages.push({ name: name, at: new Date().toISOString(), stats: stats })
  label.text = 'Memory check: ' + name; save(); return stats
}
function fail (message) { invalid = true; clearInterval(poll); clearInterval(pressure); report.error = message; synth.shutdown(); save(); label.text = message }
function begin () {
  var baseline = snapshot('before-start')
  var request = baseline.physicalPreferredSampleRateRequest
  if (!request || !request.accepted) return fail('Explicit diagnostic sample rate required')
  report.preferredSampleRateRequest = request
  if (!synth.start({ maxVoices: 32, maxSequences: 4, mixingProfile: 'conservative', audioSession: 'playback' })) return fail('Start rejected')
  synth.setVolume({ volume: 0.15 }); synth.setStatsEnabled(true); synth.setTempo({ bpm: 120 })
  if (!synth.setMasterProcessing({ chorus: { enabled: true }, delay: { enabled: true },
    reverb: { enabled: true }, saturation: { enabled: true, drive: 2 } })) return fail('Effects rejected')
  for (var k = 0; k < 4; k++) {
    var notes = []; for (var n = 0; n < 8; n++) notes.push(44 + k * 8 + n)
    var id = synth.playSequence({ loop: true, stepDuration: 1, bus: 'music', effectSend: 1,
      voice: { volume: 0.01, attack: 2, release: 30, waveType: synth.getDefaults().waveTypes.SQUARE,
        filterType: 'lowpass', filterHz: 1200, spatialX: 0.6, spatialY: -0.4,
        modulations: [{ target: 'filterHz', rate: 2, depth: 0.3 }, { target: 'pulseWidth', rate: 3, depth: 0.3 }] },
      steps: [{ notes: notes, gate: 1 }] })
    if (!(id > 0)) return fail('Sequence rejected')
    ids.push(id)
  }
  report.ids = ids
  poll = setInterval(function () { var stats = synth.getStats(); report.polls = (report.polls || 0) + 1; report.latestActiveVoices = stats.activeVoices }, 200)
  setTimeout(dense, 15000)
}
function dense () {
  if (invalid) return
  var load = snapshot('full-load')
  var session = load.physicalAudioSession, rate = report.preferredSampleRateRequest.requested
  if (load.activeVoices !== 32 || !session || load.sampleRate !== rate || session.sessionSampleRate !== rate || session.sourceNodeSampleRate !== rate || session.outputNodeSampleRate !== rate) return fail('Full load or actual sample rate mismatch')
  var steps = []
  for (var i = 0; i < 512; i++) {
    var notes = []; for (var n = 0; n < 32; n++) notes.push(36 + n)
    steps.push({ notes: notes, gate: 1 })
  }
  snapshot('caller-score-ready')
  var rounds = 0
  pressure = setInterval(function () {
    if (invalid) return
    ids.forEach(function (id) {
      if (synth.updateSequence({ sequenceId: id, steps: steps })) report.editsAccepted++
      else report.editsRejected++
    })
    if (++rounds === 30) {
      clearInterval(pressure); snapshot('publication-pressure-completed'); steps = null
      synth.stopAll()
      setTimeout(function () {
        if (invalid) return
        snapshot('before-shutdown'); synth.shutdown()
        setTimeout(function () {
          if (invalid) return
          snapshot('shutdown-settled')
          report.restartAccepted = synth.start({ maxVoices: 32, maxSequences: 4, audioSession: 'playback' })
          if (!report.restartAccepted) return fail('Restart rejected')
          synth.setVolume({ volume: 0.15 }); snapshot('restart-allocated')
          setTimeout(function () {
            if (invalid) return
            snapshot('restart-settled'); synth.shutdown()
            setTimeout(function () {
              if (invalid) return
              clearInterval(poll); snapshot('final-shutdown'); report.complete = true; save()
            }, 2000)
          }, 30000)
        }, 30000)
      }, 2000)
    }
  }, 1000)
}
Ti.App.addEventListener('pause', function () { if (!report.complete) fail('App paused; memory run invalid') })
win.addEventListener('open', function () { setTimeout(begin, 5000) })
win.open()
