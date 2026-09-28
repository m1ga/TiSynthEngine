const assert = require('assert')
const fs = require('fs')
const path = require('path')

const root = path.resolve(__dirname, '..')
const androidModule = fs.readFileSync(
  path.join(root, 'android/src/ti/synthengine/TiSynthEngineModule.java'),
  'utf8'
)
const androidContract = fs.readFileSync(
  path.join(root, 'android/src/ti/synthengine/SynthContract.java'),
  'utf8'
)
const iosModule = fs.readFileSync(
  path.join(root, 'ios/Classes/TiSynthengineModule.m'),
  'utf8'
)
const iosContract = fs.readFileSync(
  path.join(root, 'ios/Classes/TiSynthengineContract.m'),
  'utf8'
)

const expected = {
  DEFAULT_FREQUENCY_HZ: 440.0,
  DURATION_DEFAULT_MS: 500,
  ATTACK_DEFAULT_MS: 50,
  RELEASE_DEFAULT_MS: 50,
  VOLUME_DEFAULT: 1.0
}

function readAndroidConstant(name) {
  const contractNames = {
    DEFAULT_FREQUENCY_HZ: 'DEFAULT_FREQUENCY_HZ',
    DURATION_DEFAULT_MS: 'DEFAULT_DURATION_MS',
    ATTACK_DEFAULT_MS: 'DEFAULT_ATTACK_MS',
    RELEASE_DEFAULT_MS: 'DEFAULT_RELEASE_MS',
    VOLUME_DEFAULT: 'DEFAULT_VOLUME'
  }
  const match = androidContract.match(new RegExp(`${contractNames[name]}\\s*=\\s*([0-9.]+)`))
  assert(match, `Android is missing ${name}`)
  return Number(match[1])
}

function readIosConstant(name) {
  const contractNames = {
    DEFAULT_FREQUENCY_HZ: 'ContractDefaultFrequency',
    DURATION_DEFAULT_MS: 'ContractDefaultDuration',
    ATTACK_DEFAULT_MS: 'ContractDefaultAttack',
    RELEASE_DEFAULT_MS: 'ContractDefaultRelease',
    VOLUME_DEFAULT: 'ContractDefaultVolume'
  }
  const match = iosContract.match(new RegExp(`${contractNames[name]}\\s*=\\s*([0-9.]+)`))
  assert(match, `iOS is missing ${name}`)
  return Number(match[1])
}

Object.keys(expected).forEach(function (name) {
  assert.strictEqual(readAndroidConstant(name), expected[name], `Android ${name}`)
  assert.strictEqual(readIosConstant(name), expected[name], `iOS ${name}`)
})

assert(
  /readOptionalIntOption\(options, "playTone", "duration",\s*DURATION_DEFAULT_MS/.test(androidModule),
  'Android playTone duration must use the default profile'
)
assert(
  /readOptionalIntOption\(options, "playChord", "duration",\s*DURATION_DEFAULT_MS/.test(androidModule),
  'Android playChord duration must use the default profile'
)
assert(
  /method:@"playTone" key:@"duration"\s*defaultValue:DURATION_DEFAULT_MS/.test(iosModule),
  'iOS playTone duration must use the default profile'
)
assert(
  /method:@"playChord" key:@"duration"\s*defaultValue:DURATION_DEFAULT_MS/.test(iosModule),
  'iOS playChord duration must use the default profile'
)

console.log('default_profile_regression: all tests passed')
