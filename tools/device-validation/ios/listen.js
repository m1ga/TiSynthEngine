/* Preview-only entry point: no stress test or automatic sound. */
var synth = require('ti.synthengine')
var WAVE = synth.getDefaults().waveTypes
var win = Ti.UI.createWindow({ backgroundColor: '#161616' })
var label = Ti.UI.createLabel({ top: 70, left: 30, right: 30, color: '#ffffff', text: 'TiSynthEngine listening preview\nUse a comfortable device/headphone level.' })
var play = Ti.UI.createButton({ title: 'Play quiet native phrase', top: 180 })
var stop = Ti.UI.createButton({ title: 'Stop audio', top: 250 })
win.add(label); win.add(play); win.add(stop)
play.addEventListener('click', function () {
  if (!synth.start({ maxVoices: 32, maxSequences: 4, audioSession: 'playback' })) return
  synth.setVolume({ volume: 0.2 })
  synth.setTempo({ bpm: 120 })
  synth.setMasterProcessing({ applyTo: ['music'], reverb: { enabled: true, mix: 0.12 } })
  var id = synth.playSequence({ bus: 'music', stepDuration: 0.5,
    voice: { waveType: WAVE.TRIANGLE, attack: 8, release: 180, volume: 0.08 },
    steps: [{ notes: [60] }, { notes: [64] }, { notes: [67] }, { notes: [60, 64, 67], length: 1.5 }] })
  label.text = id > 0 ? 'Native phrase accepted. Listen for attacks, overlap and the reverb tail.' : 'Sequence rejected'
})
stop.addEventListener('click', function () { synth.stopAll(); synth.shutdown(); label.text = 'Audio stopped' })
Ti.App.addEventListener('pause', function () { synth.shutdown() })
win.addEventListener('close', function () { synth.shutdown() })
win.open()
