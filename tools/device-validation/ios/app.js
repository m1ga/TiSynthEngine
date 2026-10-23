/* Isolated physical acceptance runner. Never writes Studio preferences. */
Ti.App.idleTimerDisabled = true
var synth = require('ti.synthengine')
var WAVE = synth.getDefaults().waveTypes
var report = { schemaVersion: 1, platform: 'ios', appId: Ti.App.id, model: Ti.Platform.model,
  osVersion: Ti.Platform.version, protocol: { warmupSeconds: 15, measuredSeconds: 600, voices: 32,
    sequences: 4, binaural: true, lfosPerVoice: 2, processors: ['chorus', 'delay', 'reverb', 'saturation'] },
  startedAt: new Date().toISOString(), observations: [], acceptanceComplete: false }
var win = Ti.UI.createWindow({ backgroundColor: '#161616' })
var label = Ti.UI.createLabel({ color: '#ffffff', text: 'Physical audio acceptance\nPreparing isolated app', left: 30, right: 30, textAlign: 'center' })
win.add(label)
var file = Ti.Filesystem.getFile(Ti.Filesystem.applicationDataDirectory, 'physical-report.json')
function writeReport (stage) { report.stage = stage; file.write(JSON.stringify(report)); Ti.API.info('SYNTH_PHYSICAL ' + JSON.stringify(report)) }
function fail (message) { report.failure = message; writeReport('failed'); label.text = message; synth.shutdown() }
var ids = []
function begin () {
  var baseline = synth.getStats({ physicalMemory: true })
  report.baselineProcessMemory = baseline.physicalProcessMemory
  report.baselineAudioSession = baseline.physicalAudioSession
  report.beforeStartSystemAvailableMemory = Ti.Platform.availableMemory
  if (!synth.start({ maxVoices: 32, maxSequences: 4, mixingProfile: 'conservative', audioSession: 'playback' })) return fail('start rejected')
  synth.setVolume({ volume: 0.15 })
  synth.setStatsEnabled(true)
  if (!synth.setTempo({ bpm: 120 })) return fail('tempo rejected')
  if (!synth.setMasterProcessing({ applyTo: ['music', 'effects', 'ui', 'ambience'],
    chorus: { enabled: true }, delay: { enabled: true }, reverb: { enabled: true }, saturation: { enabled: true, drive: 2 } })) return fail('processing rejected')
  for (var slot = 0; slot < 4; slot++) {
    var notes = []; for (var n = 0; n < 8; n++) notes.push(44 + slot * 8 + n)
    var id = synth.playSequence({ loop: true, stepDuration: 1, bus: 'music', effectSend: 1,
      voice: { volume: 0.01, attack: 2, release: 30, waveType: WAVE.SQUARE, filterType: 'lowpass', filterHz: 1200,
        spatialX: 0.6, spatialY: -0.4, modulations: [{ target: 'filterHz', rate: 2, depth: 0.3 }, { target: 'pulseWidth', rate: 3, depth: 0.3 }] },
      steps: [{ notes: notes, gate: 1 }] })
    if (!(id > 0)) return fail('sequence rejected: ' + slot)
    ids.push(id)
  }
  report.sequenceIds = ids
  report.warmupStartedAt = new Date().toISOString()
  label.text = '32 voices / 4 sequences\nWarming up 15 seconds'
  writeReport('warmup')
  setTimeout(measure, 15000)
}
var timer
function measure () {
  var feasibility = synth.getStats({ physicalMemory: true })
  report.feasibility = feasibility
  if (!feasibility.physicalTelemetry || feasibility.physicalTelemetry.callbacks < 100) return fail('physical telemetry missing')
  if (feasibility.activeVoices !== 32 || ids.some(function (id) { return !synth.getSequenceState({ sequenceId: id }) })) return fail('full load not active')
  synth.resetStats()
  var started = Date.now()
  report.measurementStartedAt = new Date(started).toISOString()
  report.measurementStartEpochMs = started
  label.text = 'Controlled ten-minute load\nDo not interact with this iPad'
  writeReport('measuring')
  timer = setInterval(function () {
    var elapsed = Date.now() - started
    var stats = synth.getStats()
    var telemetry = stats.physicalTelemetry
    stats.physicalTelemetry = { callbacks: telemetry.callbacks, renderedFrames: telemetry.renderedFrames, p95Upper: telemetry.p95Upper, deadlineMisses: telemetry.deadlineMisses, frozen: telemetry.frozen }
    report.observations.push({ elapsedMs: elapsed, stats: stats, positions: ids.map(function (id) { return synth.getSequenceState({ sequenceId: id }) }) })
    report.latest = stats
    if (elapsed >= 600000 && stats.physicalTelemetry.renderedFrames / stats.sampleRate >= 600) {
      clearInterval(timer)
      synth.getStats({ physicalFreeze: true })
      setTimeout(function () { finishMeasurement(started) }, 100)
    } else if (report.observations.length % 10 === 0) {
      label.text = 'Controlled load: ' + Math.floor(elapsed / 1000) + ' / 600 seconds\n32 voices / 4 sequences'
      writeReport('measuring')
    }
  }, 1000)
}
function finishMeasurement (started) {
      var elapsed = Date.now() - started
      var stats = synth.getStats({ physicalMemory: true })
      report.finalTelemetry = stats.physicalTelemetry
      report.fullLoadProcessMemory = stats.physicalProcessMemory
      report.fullLoadAudioSession = stats.physicalAudioSession
      report.measuredAudioSeconds = stats.physicalTelemetry.renderedFrames / stats.sampleRate
      report.measuredDurationMs = elapsed
      report.measurementFinishedAt = new Date().toISOString()
      report.sampleRate = stats.sampleRate
      report.accumulatedRenderLoadMaximum = stats.peakRenderLoad
      report.percentileBasis = 'Every measured callback counted in frozen native histogram; bins have exclusive upper load bound at (index+1)/1000.'
      report.renderBoundPassed = stats.physicalTelemetry.frozen && typeof stats.physicalTelemetry.p95Upper === 'number' && stats.physicalTelemetry.p95Upper <= 0.5
      report.measuredCallbackDeadlineMisses = stats.physicalTelemetry.deadlineMisses
      report.renderTimingPassed = report.renderBoundPassed && report.measuredCallbackDeadlineMisses === 0 && stats.physicalTelemetry.invalidNegativeOrNaN === 0 && report.measuredAudioSeconds >= 600
      report.outputUnderrunCount = stats.underrunCount
      report.systemAvailableMemoryAfterLoad = Ti.Platform.availableMemory
      report.rejections = stats.rejectedRequestsByReason
      synth.stopAll()
      setTimeout(function () {
        report.afterPanic = synth.getStats()
        report.systemAvailableMemoryAfterPanic = Ti.Platform.availableMemory
        label.text = 'Ten-minute run finished\n' + (report.renderBoundPassed ? 'Render p95 upper bound below 50%' : 'Render acceptance requires detailed review')
        writeReport('timed-load-completed')
        setTimeout(memoryPressure, 3000)
      }, 2000)
}
win.addEventListener('open', function () {
  writeReport('baseline-ready')
  label.text = 'Baseline ready\nStarting load in 5 seconds'
  setTimeout(begin, 5000)
})
Ti.App.addEventListener('pause', function () { report.backgroundedAt = new Date().toISOString(); writeReport('interrupted-background') })
win.open()

function memoryPressure () {
  var steps = []
  for (var i = 0; i < 512; i++) {
    var notes = []
    for (var n = 0; n < 32; n++) notes.push(36 + n)
    steps.push({ notes: notes, overrides: { volume: 0.0005, pan: (i % 5 - 2) / 2 } })
  }
  var pressureIds = []
  for (var slot = 0; slot < 4; slot++) pressureIds.push(synth.playSequence({ loop: true, stepDuration: 1, steps: steps }))
  report.memoryPressure = { stepsPerSequence: 512, notesPerStep: 32, sequences: pressureIds, acceptedEdits: 0, rejectedEdits: 0 }
  writeReport('high-density-publication')
  var count = 0
  var pressure = setInterval(function () {
    pressureIds.forEach(function (id) {
      if (synth.updateSequence({ sequenceId: id, steps: steps })) report.memoryPressure.acceptedEdits++
      else report.memoryPressure.rejectedEdits++
    })
    if (++count >= 30) {
      clearInterval(pressure)
      synth.stopAll()
      setTimeout(function () {
        report.memoryPressure.preShutdownMemory = synth.getStats({ physicalMemory: true }).physicalProcessMemory
        synth.shutdown()
        report.memoryPressure.postPressureMemory = synth.getStats({ physicalMemory: true }).physicalProcessMemory
        report.memoryPressure.finishedAt = new Date().toISOString()
        writeReport('memory-shutdown')
        setTimeout(function () {
          report.recoveryRestartAccepted = synth.start({ maxVoices: 32, maxSequences: 4, audioSession: 'playback' })
          report.memoryPressure.restartMemory = synth.getStats({ physicalMemory: true }).physicalProcessMemory
          writeReport('memory-restart')
          setTimeout(function () {
            synth.shutdown()
            report.memoryPressure.finalShutdownMemory = synth.getStats({ physicalMemory: true }).physicalProcessMemory
            label.text = 'Physical run finished.\nReports saved. Listening control ready.'
            listen.enabled = true
            writeReport('completed')
          }, 30000)
        }, 30000)
      }, 2000)
    }
  }, 1000)
}

var listen = Ti.UI.createButton({ title: 'Listen: quiet native phrase', bottom: 40, enabled: false })
win.add(listen)
listen.addEventListener('click', function () {
  if (!synth.start({ maxVoices: 32, maxSequences: 4, audioSession: 'playback' })) return
  synth.setVolume({ volume: 0.2 })
  synth.setTempo({ bpm: 120 })
  synth.setMasterProcessing({ applyTo: ['music'], reverb: { enabled: true, mix: 0.12 } })
  synth.playSequence({ bus: 'music', stepDuration: 0.5,
    voice: { waveType: WAVE.TRIANGLE, attack: 8, release: 180, volume: 0.08 },
    steps: [{ notes: [60] }, { notes: [64] }, { notes: [67] }, { notes: [60, 64, 67], length: 1.5 }] })
})
