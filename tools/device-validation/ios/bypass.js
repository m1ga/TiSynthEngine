/* Isolated native-rate idle/stereo/binaural screen; no effects or LFOs. */
Ti.App.idleTimerDisabled = true
var synth = require('ti.synthengine')
var wave = synth.getDefaults().waveTypes.SINE
var file = Ti.Filesystem.getFile(Ti.Filesystem.applicationDataDirectory, 'bypass-report.json')
var report = { schemaVersion: 1, startedAt: new Date().toISOString(), complete: false,
  protocol: { warmupSeconds: 10, measuredAudioSecondsPerPhase: 30, maxVoices: 32,
    maxSequences: 4, masterVolume: 0.15, effects: false, lfos: false }, phases: [] }
var phases = [{ name: 'silence', voices: 0 }, { name: 'one-stereo', voices: 1 },
  { name: '32-stereo', voices: 32 }, { name: '32-binaural', voices: 32, spatial: true }]
var win = Ti.UI.createWindow({ backgroundColor: '#161616' })
var label = Ti.UI.createLabel({ color: '#ffffff', text: 'Native bypass screen', left: 30, right: 30 })
win.add(label)
var index = 0, nextVoiceId = 1, interrupted = false
function save () { file.write(JSON.stringify(report)) }
function fail (message) { interrupted = true; report.error = message; save(); synth.shutdown(); label.text = message }
function routeSignature (session) { return JSON.stringify(session.ports.map(function (port) { return [port.type, port.name] })) }
function actualFormats (stats) {
  var rate = report.preferredSampleRateRequest.requested
  var session = stats.physicalAudioSession
  return session && stats.sampleRate === rate && session.sessionSampleRate === rate &&
    session.sourceNodeSampleRate === rate && session.outputNodeSampleRate === rate
}
function next () {
  if (interrupted) return
  if (index === phases.length) { synth.shutdown(); report.complete = true; save(); label.text = 'Bypass screen completed'; return }
  var phase = phases[index]
  label.text = phase.name + '\n10 seconds warmup / 30 seconds measured audio'
  for (var i = 0; i < phase.voices; i++) {
    var options = { voiceId: nextVoiceId++, frequency: 220 * Math.pow(2, i / 12), waveType: wave, volume: 0.01, attack: 2, release: 30 }
    if (phase.spatial) { options.spatialX = 0.6; options.spatialY = -0.4 }
    if (!synth.noteOn(options)) return fail('Held voice rejected: ' + phase.name)
  }
  setTimeout(function () {
    if (interrupted) return
    var before = synth.getStats({ physicalMemory: true })
    if (!actualFormats(before) || before.activeVoices !== phase.voices) return fail('Rate or voice mismatch: ' + phase.name)
    var started = Date.now()
    synth.resetStats()
    var timer = setInterval(function () {
      if (interrupted) { clearInterval(timer); return }
      if (Date.now() - started > 90000) { clearInterval(timer); return fail('Audio frame clock stalled: ' + phase.name) }
      var stats = synth.getStats()
      if (stats.activeVoices !== phase.voices) { clearInterval(timer); return fail('Voice count changed: ' + phase.name) }
      if (stats.physicalTelemetry.renderedFrames / stats.sampleRate >= 30 && Date.now() - started >= 30000) {
        clearInterval(timer)
        synth.getStats({ physicalFreeze: true })
        var attempts = 0
        function frozen () {
          if (interrupted) return
          var final = synth.getStats({ physicalMemory: true })
          if (!final.physicalTelemetry.frozen) {
            if (++attempts < 100) return setTimeout(frozen, 20)
            return fail('Freeze not acknowledged: ' + phase.name)
          }
          if (!actualFormats(final) || final.callbackFrames !== before.callbackFrames || final.activeVoices !== phase.voices) return fail('Format or voice count changed: ' + phase.name)
          if (routeSignature(final.physicalAudioSession) !== routeSignature(before.physicalAudioSession) || final.physicalAudioSession.systemOutputVolume !== before.physicalAudioSession.systemOutputVolume || final.physicalAudioSession.ioBufferDuration !== before.physicalAudioSession.ioBufferDuration) return fail('Route, volume or buffer changed: ' + phase.name)
          report.phases.push({ name: phase.name, voices: phase.voices, spatial: !!phase.spatial,
            elapsedMs: Date.now() - started, measuredAudioSeconds: final.physicalTelemetry.renderedFrames / final.sampleRate,
            before: before, final: final })
          save(); synth.stopAll(); index++; setTimeout(next, 1000)
        }
        setTimeout(frozen, 100)
      }
    }, 1000)
  }, 10000)
}
win.addEventListener('open', function () {
  report.baseline = synth.getStats({ physicalMemory: true })
  report.preferredSampleRateRequest = report.baseline.physicalPreferredSampleRateRequest
  if (!report.preferredSampleRateRequest || !report.preferredSampleRateRequest.accepted) return fail('Explicit diagnostic rate preference required')
  if (!synth.start({ maxVoices: 32, maxSequences: 4, mixingProfile: 'conservative', audioSession: 'playback' })) return fail('Start rejected')
  synth.setVolume({ volume: 0.15 }); synth.setStatsEnabled(true); save(); next()
})
Ti.App.addEventListener('pause', function () { if (!report.complete) fail('App paused; bypass run invalid') })
win.open()
