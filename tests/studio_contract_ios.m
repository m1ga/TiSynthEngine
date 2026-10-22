#import <Foundation/Foundation.h>
#import "../ios/Classes/TiSynthengineContract.h"
#include "../shared/synth_sequence.h"
#include "../shared/synth_effects.h"
#include <assert.h>
int main(void) { @autoreleasepool {
  SynthSequenceConfig *c=calloc(1,sizeof(*c));
  NSDictionary *valid=@{@"steps":@[@{@"notes":@[@60,@60,@67]},@{@"rest":@YES}],@"voice":@{@"volume":@.2}};
  assert((![TiSynthengineContract prepareSequence:valid config:c maxFrequency:21600]));
  assert((c->steps[0].noteCount==3 && c->steps[0].notes[1]==60));
  assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@119]}],@"detune":@1200} config:c maxFrequency:7200]));
  assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@60],@"rest":@YES}]} config:c maxFrequency:21600]));
  assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@60.5]}]} config:c maxFrequency:21600]));
  assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"rest":@YES}],@"loop":@1} config:c maxFrequency:21600]));
  assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"rest":@YES}],@"voice":@{@"frequency":@440}} config:c maxFrequency:21600]));
  SynthEffectsConfig f=synth_effects_default_config();
  assert((![TiSynthengineContract prepareMasterProcessing:@{@"delay":@{@"enabled":@YES,@"feedback":@.7}} config:&f]));
  assert((f.delay.enabled && f.delay.feedback==.7 && f.inputGain==1));
  assert(([TiSynthengineContract prepareMasterProcessing:@{@"delay":@{@"feedback":@.86}} config:&f]));
  assert(([TiSynthengineContract prepareMasterProcessing:@{@"chorus":@{@"enabled":@1}} config:&f]));
  assert(([TiSynthengineContract prepareMasterProcessing:@{@"wet":@1} config:&f]));
  assert((![TiSynthengineContract validateNoteOn:@{@"voiceId":@1,@"waveType":@1,@"modulations":@[@{@"wave":@"triangle",@"target":@"pulseWidth",@"rate":@2,@"depth":@.4}]} maxFrequency:21600]));
  assert(([TiSynthengineContract validateNoteOn:@{@"voiceId":@1,@"waveType":@0,@"modulations":@[@{@"wave":@"sine",@"target":@"pulseWidth",@"rate":@2,@"depth":@.4}]} maxFrequency:21600]));
  assert((![TiSynthengineContract validateNoteOn:@{@"voiceId":@1,@"fmRatio":@3,@"fmDepth":@0,@"modulations":@[@{@"target":@"fmDepth",@"depth":@.3}]} maxFrequency:21600]));
  NSDictionary *merged=[TiSynthengineContract mergeSequenceVoice:@{@"waveType":@0,@"fmRatio":@3.5,@"fmDepth":@2.2,@"filterType":@"lowpass",@"filterHz":@900} changes:@{@"waveType":@3,@"filterType":@"none"}];
  assert((!merged[@"fmRatio"]&&!merged[@"fmDepth"]&&!merged[@"filterHz"]));
  assert((![TiSynthengineContract prepareSequence:@{@"voice":merged,@"steps":@[@{@"notes":@[@60]}]} config:c maxFrequency:21600]));
  NSDictionary *fmEdit=[TiSynthengineContract mergeSequenceVoice:@{@"fmRatio":@3.5,@"fmDepth":@2,@"modulations":@[@{@"target":@"fmDepth",@"depth":@.2}]} changes:@{@"fmDepth":@0}];
  assert(([fmEdit[@"fmRatio"] doubleValue]==3.5));
  assert((![TiSynthengineContract prepareSequence:@{@"tuning":@220,@"swing":@-.49,@"steps":@[@{@"notes":@[@60]}]} config:c maxFrequency:7200]));
  assert((c->tuning==220&&c->swing==-.49));
  assert((![TiSynthengineContract prepareSequence:@{@"tuning":@880,@"steps":@[@{@"notes":@[@60]}]} config:c maxFrequency:7200]));
  assert(([TiSynthengineContract prepareSequence:@{@"tuning":@880,@"steps":@[@{@"notes":@[@119]}]} config:c maxFrequency:7200]));
  SynthSequenceState snapshot={0};snapshot.playing=1;snapshot.stepIndex=2;snapshot.bpm=120;
  NSDictionary *stateObject=[TiSynthengineContract sequenceStateObject:&snapshot];
  assert((CFGetTypeID((__bridge CFTypeRef)stateObject[@"playing"])==CFBooleanGetTypeID()));
  assert(([stateObject[@"playing"] boolValue]));
  snapshot.playing=0;stateObject=[TiSynthengineContract sequenceStateObject:&snapshot];
  assert((CFGetTypeID((__bridge CFTypeRef)stateObject[@"playing"])==CFBooleanGetTypeID()&&![stateObject[@"playing"] boolValue]));
  free(c); puts("Studio iOS contracts PASS");
}}
