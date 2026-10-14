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


// Compare method tables with the native allowlists, not just mentions elsewhere.
const nativeContract = read('android/src/ti/synthengine/SynthContract.java')
const nativeModule = read('android/src/ti/synthengine/TiSynthEngineModule.java')
const nativeIosContract = read('ios/Classes/TiSynthengineContract.m')
function documentedKeys (section) {
  const keys = []
  for (const row of section.split('\n').filter(line => line.startsWith('|'))) {
    for (const match of row.split('|')[1].matchAll(/`([^`]+)`/g)) {
      keys.push(...match[1].split(',').map(key => key.trim()))
    }
  }
  return [...new Set(keys)].sort()
}
function nativeIosKeys (method, target) {
  const source = nativeIosContract.split('validate' + method + ':')[1]
  assert(source, 'iOS validator exists: ' + method)
  const list = source.match(new RegExp('RejectUnknownOptions\\(' + target + ',\\s*@\\[([\\s\\S]*?)\\]\\)'))
  assert(list, 'iOS allowlist exists: ' + method + '/' + target)
  return [...new Set([...list[1].matchAll(/@"(\w+)"/g)].map(match => match[1]))].sort()
}
function nativeKeys (method, target) {
  const source = nativeContract.split('static Validation validate' + method + '(')[1]
  assert(source, 'native validator exists: ' + method)
  const list = source.match(new RegExp('rejectUnknownOptions\\(' + target + ',([\\s\\S]*?)\\);'))
  assert(list, 'native allowlist exists: ' + method + '/' + target)
  return [...new Set([...list[1].matchAll(/"(\w+)"/g)].map(match => match[1]))].sort()
}
for (const [method, validator] of [['startEngine', 'StartEngine'], ['playTone', 'Tone'],
  ['playChord', 'Chord'], ['playPattern', 'Pattern'], ['setVolume', 'SetVolume']]) {
  const section = apiDoc.split('## `' + method + '(options)`')[1].split('\n## ')[0]
  const rootTable = method === 'playPattern' ? section.split('Step keys:')[0] : section
  assert.deepStrictEqual(documentedKeys(rootTable), nativeKeys(validator, 'options'),
    method + ' table covers exactly the native options')
  assert.deepStrictEqual(documentedKeys(rootTable), nativeIosKeys(validator, 'options'),
    method + ' table covers exactly the iOS options')
  if (method === 'playPattern') {
    assert.deepStrictEqual(documentedKeys(section.split('Step keys:')[1]), nativeKeys('Pattern', 'step'),
      'pattern step table covers exactly the native options')
    assert.deepStrictEqual(documentedKeys(section.split('Step keys:')[1]), nativeIosKeys('Pattern', 'step'),
      'pattern step table covers exactly the iOS options')
  }
}
const defaultObject = apiDoc.split('The result has this shape:')[1].match(/```javascript\s*\n([\s\S]*?)```/)[1]
const reported = vm.runInNewContext('(' + defaultObject + ')')
const nativeDefaultKeys = [...nativeModule.matchAll(/defaults\.put\("(\w+)"/g)].map(match => match[1]).sort()
assert.deepStrictEqual(Object.keys(reported.defaults).sort(), nativeDefaultKeys,
  'getDefaults example contains every native default')
const limitTable = apiDoc.split('## Limits')[1].split('\n## ')[0]
const nativeLimitKeys = [...nativeModule.matchAll(/limits\.put\("(\w+)"/g)].map(match => match[1])
for (const key of nativeLimitKeys) {
  assert(limitTable.includes('`' + key + '`') || documentedKeys(limitTable).includes(key),
    'limits table identifies getDefaults().limits.' + key)
}


const nativeConstants = new Map()
for (const [prefix, source] of [['SynthContract.', nativeContract], ['', nativeModule]]) {
  for (const match of source.matchAll(/static final (?:int|double|String)\s+(\w+)\s*=\s*([^;]+);/g)) {
    nativeConstants.set(prefix + match[1], match[2].trim())
  }
}
function nativeDefaultValue (expression) {
  if (expression === 'new Object[]{}') return []
  if (/^".*"$/.test(expression)) return JSON.parse(expression)
  if (/^-?[\d.]+$/.test(expression)) return Number(expression)
  const key = expression
  assert(nativeConstants.has(key), 'resolve native default ' + key)
  return nativeDefaultValue(nativeConstants.get(key))
}
for (const match of nativeModule.matchAll(/defaults\.put\("(\w+)",\s*([^;]+)\);/g)) {
  assert.deepStrictEqual(JSON.parse(JSON.stringify(reported.defaults[match[1]])), nativeDefaultValue(match[2].trim()),
    'getDefaults example has the native value of ' + match[1])
}

// Execute the integration example; syntax alone cannot catch reversed Y or
// accidentally leaving binaural fields present when the app switch is off.
const integration = guideDoc.split('## Integrate binaural in a 2D app')[1]
assert(integration, 'the app integration guide exists')
const placementExample = [...integration.matchAll(/```javascript\s*\n([\s\S]*?)```/g)]
  .find(match => match[1].includes('function playPositionedTone'))[1]
const requests = []
const sandbox = {
  supportsBinaural: true,
  contract: { waveTypes: { NOISE: 4 } },
  synth: { playTone: options => { requests.push(JSON.parse(JSON.stringify(options))); return true } },
  Ti: { API: { warn: () => {} } }
}
vm.runInNewContext(placementExample, sandbox)
sandbox.audioSettings.horizontalPercent = 100
const originalRecipe = { frequency: 880, duration: 400, pan: 0, panEnd: 0 }
for (const [x, y, expectedX, expectedY] of [[0, 0, -1, 1], [500, 0, 0, 1], [1000, 0, 1, 1],
  [0, 400, -1, 0], [500, 400, 0, 0], [1000, 400, 1, 0],
  [0, 800, -1, -1], [500, 800, 0, -1], [1000, 800, 1, -1]]) {
  sandbox.playPositionedTone(originalRecipe, x, y, 1000, 800)
  const options = requests.at(-1)
  assert.strictEqual(options.spatialX, expectedX)
  assert.strictEqual(options.spatialY, expectedY)
  assert(!('pan' in options) && !('panEnd' in options), 'binaural removes stereo fields')
}
assert.deepStrictEqual(originalRecipe, { frequency: 880, duration: 400, pan: 0, panEnd: 0 },
  'placement preserves the synthesis recipe')
sandbox.audioSettings.heightPercent = 0
sandbox.playPositionedTone(originalRecipe, 1000, 800, 1000, 800)
assert.strictEqual(Math.abs(requests.at(-1).spatialY), 0, 'zero height centers binaural elevation')
assert('spatialX' in requests.at(-1), 'zero height does not disable binaural')
sandbox.audioSettings.horizontalPercent = 50
sandbox.playPositionedTone(originalRecipe, 1000, 400, 1000, 800)
assert.strictEqual(requests.at(-1).spatialX, 0.5, 'horizontal slider scales direction')
sandbox.audioSettings.binaural = false
sandbox.playPositionedTone(originalRecipe, 1000, 0, 1000, 800)
assert.strictEqual(requests.at(-1).pan, 0.5)
assert(!('spatialX' in requests.at(-1)) && !('spatialY' in requests.at(-1)),
  'the off switch omits coordinates')
sandbox.audioSettings.binaural = true
sandbox.supportsBinaural = false
sandbox.playPositionedTone(originalRecipe, 1000, 800, 1000, 800)
assert.strictEqual(requests.at(-1).pan, 0.5, 'a module without spatial support uses stereo')
assert(!('spatialY' in requests.at(-1)))
assert.throws(() => sandbox.playPositionedTone(originalRecipe, 0, 0, 0, 800),
  /positive dimensions/, 'invalid arena dimensions are rejected')

// Execute rhythm examples so a short voice duration cannot silently replace
// the tempo grid advertised by the surrounding documentation.
function patternExample(source, heading) {
  const section = source.split(heading + '\n')[1].split(/\n#{1,3} /)[0]
  const code = section.match(/```javascript\s*\n([\s\S]*?)```/)[1]
  const patterns = []
  vm.runInNewContext(code, {
    synth: { playPattern: options => { patterns.push(options); return true } },
    WAVE: { SINE: 0, SQUARE: 1, SAWTOOTH: 2, TRIANGLE: 3, NOISE: 4 }
  })
  assert.strictEqual(patterns.length, 1, heading + ' submits one pattern')
  return patterns[0]
}
function rhythm(pattern) {
  let elapsed = 0
  return Array.from(pattern.steps, (step, index) => {
    const value = step.noteValue || pattern.noteValue
    const quarter = 60000 / (pattern.bpm || 120)
    const noteMs = { '4n': quarter, '8n': quarter / 2, '16n': quarter / 4 }[value]
    const duration = step.duration ?? noteMs ?? pattern.stepDuration ?? 100
    const start = Math.max(0, elapsed + (index % 2 ? -1 : 1) * (pattern.swing || 0) * duration)
    elapsed += duration
    return { start, duration, rest: step.rest === true }
  })
}
function closeTo(actual, expected, message) {
  assert(Math.abs(actual - expected) < 1e-7, message + ': ' + actual + ' vs ' + expected)
}
const tempoPattern = patternExample(examples, '## Tempo, note values and swing')
const tempoRhythm = rhythm(tempoPattern)
const eighthAt132 = 60000 / 132 / 2
for (let index = 0; index < tempoRhythm.length; index++) {
  closeTo(tempoRhythm[index].duration, eighthAt132 * (index === 4 ? 2 : 1),
    'tempo example retains its eighth/quarter-note durations')
}
closeTo(tempoRhythm[0].start, eighthAt132 * 0.12, 'swing uses the first resolved eighth')
closeTo(tempoRhythm[4].start, eighthAt132 * 4 + eighthAt132 * 2 * 0.12,
  'swing uses the final resolved quarter, not the root fallback')
const millisecondRhythm = rhythm(patternExample(examples, '## Millisecond pattern'))
assert.deepStrictEqual(millisecondRhythm.filter(step => !step.rest).map(step => step.start),
  [0, 110, 220, 330], '90 ms notes plus rests retain 110 ms onset spacing')
assert.strictEqual(millisecondRhythm.at(-1).duration, 180, 'the final tone retains its long ending')
for (const [source, heading, bpm, divisor] of [
  [examples, '## Noise percussion', 116, 4],
  [soundDesign, '### Noise percussion', 112, 2],
  [soundDesign, '### Retro pickup', 180, 4]
]) {
  const pattern = patternExample(source, heading)
  const grid = 60000 / bpm / divisor
  for (const [index, step] of rhythm(pattern).entries()) {
    closeTo(step.duration, grid * (heading === '### Retro pickup' && index === 3 ? 2 : 1),
      heading + ' keeps its advertised note grid')
  }
  if (heading.includes('Noise percussion')) {
    for (const step of pattern.steps.filter(step => !step.rest)) {
      assert(step.attack + step.decay < grid, 'percussion settles before the next beat')
      assert.strictEqual(step.sustain, 0, 'percussion does not hold a level through the beat')
      assert(step.release <= Math.round(grid) - step.attack, 'percussion release fits the native envelope')
    }
  }
}

console.log('documentation_regression: all tests passed')
