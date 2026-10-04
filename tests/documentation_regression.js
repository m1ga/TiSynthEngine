const assert = require('assert')
const fs = require('fs')
const path = require('path')
const vm = require('vm')

const root = path.resolve(__dirname, '..')
const publicFiles = [
  'README.md',
  'CHANGELOG.md',
  'android/README.md',
  'android/Resources/README.md',
  'android/platform/README.md',
  'ios/README.md',
  'ios/Resources/README.md',
  'ios/platform/README.md',
  'documentation/index.md',
  'documentation/guide.md',
  'documentation/api.md',
  'documentation/examples.md',
  'documentation/sound-design.md',
  'documentation/audio-engine.md',
  'documentation/recommendations.md',
  'documentation/troubleshooting.md',
  'documentation/third-party-notices.md'
]

function read(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), 'utf8')
}

function githubSlug(heading) {
  return heading
    .trim()
    .toLowerCase()
    .replace(/<[^>]+>/g, '')
    .replace(/[`*_~]/g, '')
    .replace(/[^\p{L}\p{N}\s-]/gu, '')
    .replace(/\s+/g, '-')
}

function headings(source) {
  const counts = new Map()
  const anchors = new Set()
  for (const match of source.matchAll(/^#{1,6}\s+(.+)$/gm)) {
    const base = githubSlug(match[1])
    const count = counts.get(base) || 0
    counts.set(base, count + 1)
    anchors.add(count === 0 ? base : `${base}-${count}`)
  }
  return anchors
}

function checkLinks(relativePath, source) {
  for (const match of source.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/g)) {
    const destination = match[1].trim().replace(/^<|>$/g, '')
    if (/^(?:https?:|mailto:)/.test(destination)) continue

    const split = destination.split('#')
    const targetPart = decodeURIComponent(split[0])
    const fragment = split[1] ? decodeURIComponent(split[1]) : ''
    const baseDirectory = path.dirname(path.join(root, relativePath))
    const target = targetPart ? path.resolve(baseDirectory, targetPart) : path.join(root, relativePath)

    assert(fs.existsSync(target), `${relativePath} links to missing ${destination}`)
    if (fragment && fs.statSync(target).isFile() && path.extname(target).toLowerCase() === '.md') {
      assert(headings(fs.readFileSync(target, 'utf8')).has(fragment.toLowerCase()),
        `${relativePath} links to missing heading ${destination}`)
    }
  }
}

function checkJavaScriptFences(relativePath, source) {
  let index = 0
  for (const match of source.matchAll(/```javascript\s*\n([\s\S]*?)```/g)) {
    index += 1
    const code = match[1]
    try {
      new vm.Script(code, { filename: `${relativePath}#javascript-${index}` })
    } catch (error) {
      const trimmed = code.trim()
      if (!(trimmed.startsWith('{') && trimmed.endsWith('}'))) throw error
      new vm.Script(`(${trimmed})`, { filename: `${relativePath}#javascript-${index}` })
    }
  }
}

function checkUnwrappedProse(relativePath, source) {
  let fenced = false
  let previousWasProse = false

  for (const line of source.split('\n')) {
    if (line.startsWith('```')) {
      fenced = !fenced
      previousWasProse = false
      continue
    }
    if (fenced) continue

    const isProse = line.length > 0
      && !/^(?:#{1,6}\s|[-*+]\s|\d+\.\s|>|<| {4}|\t|\|)/.test(line)
    assert(!(isProse && previousWasProse),
      `${relativePath} hard-wraps a prose paragraph`)
    previousWasProse = isProse
  }
}

for (const relativePath of publicFiles) {
  const source = read(relativePath)
  for (const version of source.match(/(?<![\d.])\d+\.\d+\.\d+/g) || []) {
    assert(version === '1.0.0' || version === '1.10.0' || /^13\.\d+\.\d+$/.test(version),
      `${relativePath} mentions version ${version}; only the module, Titanium SDK and Oboe versions are public`)
  }
  assert(!/\b(?:legacy|backwards? compatibility|migration)\b/i.test(source),
    `${relativePath} contains historical compatibility language`)
  assert(!/[—–]/.test(source), `${relativePath} contains editorial dash punctuation`)
  checkLinks(relativePath, source)
  checkJavaScriptFences(relativePath, source)
}

const readme = read('README.md')
const soundDesign = read('documentation/sound-design.md')
const examples = read('documentation/examples.md')
const exampleApp = read('example/app.js')
const allGuides = publicFiles.map(read).join('\n')

assert(readme.split('\n').length >= 250 && readme.split('\n').length <= 400,
  'README stays a complete but approachable introduction')
checkUnwrappedProse('README.md', readme)
assert(allGuides.includes('unsupported_option'), 'strict unknown-option behavior is documented')
assert(allGuides.includes('invalid_step_type'), 'invalid pattern step behavior is documented')
assert(allGuides.includes('speakerSafe'), 'speaker-safe profile is documented')
assert(allGuides.includes("audioSession: 'ambient'"), 'ambient audio session is documented')
assert(allGuides.includes('panEnd'), 'pan movement is documented')
assert(allGuides.includes('lfoFreq') && allGuides.includes('lfoDepth'), 'vibrato is documented')
assert(allGuides.includes('noiseRate') && allGuides.includes('noiseRateEnd'),
  'variable-rate noise is documented')
assert(allGuides.includes('lowPassHz'), 'low-pass filtering is documented')
assert(allGuides.includes('pulseWidth') && allGuides.includes('pulseWidthEnd'),
  'pulse-width control is documented')
assert(allGuides.includes('waveTable') && allGuides.includes('WAVETABLE'),
  'custom wavetables are documented')
assert(allGuides.includes('bitDepth'), 'amplitude quantization is documented')
assert(!/defender|williams/i.test(allGuides + '\n' + exampleApp),
  'public documentation and example stay product-neutral')
assert(examples.includes('WAVE.SAWTOOTH'), 'sawtooth example is present')
assert(examples.includes('pans: ['), 'per-note chord pan example is present')
assert(examples.includes('bpm:') && examples.includes('swing:') && examples.includes('noteValue:'),
  'tempo, swing and note-value examples are present')
assert(examples.includes('Fixed controls on a chord'),
  'fixed chord controls have a complete example')
assert(examples.includes('Wave-shaping pattern overrides'),
  'inherited and per-step controls have a complete example')
assert(soundDesign.includes('Interface click'), 'click recipe is present')
assert(soundDesign.includes('Soft pad'), 'pad recipe is present')
assert(soundDesign.includes('Noise percussion'), 'noise percussion recipe is present')
assert(soundDesign.includes('Simon-style cues'), 'Simon-style recipe is present')
assert(soundDesign.includes('not atomic'), 'independent tone calls are explained')
assert(soundDesign.includes('pumping'), 'retrigger pumping is explained')
const expressiveKeys = ['decay', 'sustain', 'filterType', 'filterHz', 'filterHzEnd', 'filterQ',
  'fmRatio', 'fmDepth', 'fmDepthEnd', 'detune', 'detunes']
const expressiveReasons = ['sustain_without_decay', 'decay_exceeds_remaining_duration',
  'filter_type_required', 'fm_requires_sine', 'fm_depth_required']
const apiDoc = read('documentation/api.md')
const guideDoc = read('documentation/guide.md')
const troubleshootingDoc = read('documentation/troubleshooting.md')
for (const key of expressiveKeys) {
  assert(apiDoc.includes('`' + key + '`'), `api.md documents ${key}`)
  assert(allGuides.includes(key + ':'), `a guide shows ${key} in code`)
}
for (const reason of expressiveReasons) {
  assert(apiDoc.includes('`' + reason + '`'), `api.md lists the ${reason} reason`)
  assert(troubleshootingDoc.includes('`' + reason + '`'), `troubleshooting.md explains ${reason}`)
}
assert(apiDoc.includes('filterTypes'), 'getDefaults reports the filter types')
assert(apiDoc.includes('## `patternend` event') && apiDoc.includes('completed'),
  'api.md documents the patternend event and its payload')
assert(readme.includes('patternend') && guideDoc.includes('removeEventListener(\'patternend\'')
  && troubleshootingDoc.includes('patternend'),
  'the event is introduced, cleaned up and troubleshot')
for (const recipe of ['Pluck', 'Electric piano', 'Bell', 'Filtered laser', 'Hi-hat', 'Thick pad', 'Kick']) {
  assert(soundDesign.includes('### ' + recipe), `sound design has the ${recipe} recipe`)
}
assert(read('documentation/audio-engine.md').includes('shared/synth_core.h'),
  'the shared DSP core is documented')
assert(!fs.existsSync(path.join(root, 'documentation/migration-3.0.md')),
  'migration document is removed from the public tree')
assert(!fs.existsSync(path.join(root, 'analisis.md')),
  'historical analysis is removed from the public tree')
assert(!fs.existsSync(path.join(root, 'docs'))
  || fs.readdirSync(path.join(root, 'docs')).every(name => name === 'project' || name === '.DS_Store'),
  'docs/ holds only the local project notes')
assert(!fs.existsSync(path.join(root, 'README-ORIGINAL.md')),
  'stale README copy is removed from the repository')

new vm.Script(exampleApp, { filename: 'example/app.js' })
assert(exampleApp.includes("apiVersion !== '1.0.0'"),
  'example checks the public API version')
assert(exampleApp.includes('WAVE.SAWTOOTH'),
  'example exposes the sawtooth waveform')
assert(exampleApp.includes('WAVE.WAVETABLE'),
  'example exposes the wavetable waveform')
assert(exampleApp.includes('noiseRateEnd:'),
  'example demonstrates a variable-rate noise sweep')
assert(exampleApp.includes('pulseWidthEnd:'),
  'example demonstrates a pulse-width sweep')
assert(exampleApp.includes('SHAPING PATTERN'),
  'example demonstrates pattern inheritance and overrides')
assert(exampleApp.includes('return Synth.playPattern({\n      stepDuration: 210,'),
  'shaping pattern exposes its acceptance result')
assert(exampleApp.includes("accepted === false"),
  'example reports rejected sound requests')
assert(exampleApp.includes('pans: ['),
  'example demonstrates explicit chord pans')
assert(exampleApp.includes('{ rest: true, noteValue:'),
  'noise pattern contains an explicit rest')
for (const key of ['decay:', 'sustain:', 'filterType:', 'filterHzEnd:', 'fmDepthEnd:', 'fmRatio:', 'detunes:']) {
  assert(exampleApp.includes(key), `example demonstrates ${key}`)
}
assert(exampleApp.includes("Synth.addEventListener('patternend'")
  && exampleApp.includes("Synth.removeEventListener('patternend'"),
  'example listens for patternend and removes the listener on close')

console.log('documentation_regression: all tests passed')
