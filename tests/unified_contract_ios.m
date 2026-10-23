#import <Foundation/Foundation.h>
#import "../ios/Classes/TiSynthengineContract.h"
#include <assert.h>
int main(void) { @autoreleasepool {
 SynthSequenceConfig *c=calloc(1,sizeof(*c));
 assert((![TiSynthengineContract validateStart:@{@"maxSequences":@4}]));
 assert(([TiSynthengineContract validateStart:@{@"maxSequences":@5}]));
 NSDictionary *options=@{@"timeUnit":@"ms",@"stepDuration":@100,@"bus":@"music",@"steps":@[@{@"frequencies":@[@440,@440],@"duration":@50,@"length":@200,@"priority":@3},@{@"rest":@YES}]};
 assert((![TiSynthengineContract prepareSequence:options config:c maxFrequency:21600]));
 assert((c->timeUnit==SYNTH_SEQUENCE_MS&&c->stepDuration==100&&!c->loop));
 assert((c->steps[0].frequencies[1]==440&&c->steps[0].length==200&&c->steps[0].duration==50));
 assert((c->steps[0].voice.bus==0&&c->steps[0].voice.priority==3));
 assert((![TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@60,@60]}]} config:c maxFrequency:21600]));
 assert((c->stepDuration==.25&&c->steps[0].gate==.8&&!c->loop));
 assert(([TiSynthengineContract prepareSequence:@{@"bpm":@120,@"steps":@[@{@"notes":@[@60]}]} config:c maxFrequency:21600]));
 assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@60],@"frequencies":@[@440]}]} config:c maxFrequency:21600]));
 assert((![TiSynthengineContract validateTone:@{@"bus":@"ui",@"effectSend":@0,@"priority":@3} maxFrequency:21600]));
 assert(([TiSynthengineContract validateTone:@{@"priority":@4} maxFrequency:21600]));
 assert(([TiSynthengineContract validateTone:@{@"bus":@"other"} maxFrequency:21600]));
 assert((![TiSynthengineContract validateUpdateVoice:@{@"voiceId":@1,@"noiseRate":@0,@"lowPassHz":@0,@"spatialX":@.5,@"effectSend":@.5} maxFrequency:21600]));
 SynthEffectsConfig fx=synth_effects_default_config();
 assert((![TiSynthengineContract prepareMasterProcessing:@{@"applyTo":@[@"music",@"ambience"]} config:&fx]));assert((fx.applyToMask==9));
 assert(([TiSynthengineContract prepareMasterProcessing:@{@"applyTo":@[@"unknown"]} config:&fx]));
 assert(([TiSynthengineContract prepareSequence:@{@"stepDuration":@.000001,@"steps":@[@{@"notes":@[@60]}]} config:c maxFrequency:21600]));
 assert(([TiSynthengineContract prepareSequence:@{@"stepDuration":@301,@"steps":@[@{@"notes":@[@60]}]} config:c maxFrequency:21600]));
 assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@60],@"overrides":@{@"frequencyEnd":@"wrong"}}]} config:c maxFrequency:21600]));
 assert(([TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@60],@"overrides":@{@"fmDepthEnd":@2,@"waveType":@1}}]} config:c maxFrequency:21600]));
 assert((![TiSynthengineContract prepareSequence:@{@"steps":@[@{@"notes":@[@60],@"overrides":@{@"fmDepth":@0,@"fmDepthEnd":@2,@"fmRatio":@3}}]} config:c maxFrequency:21600]));
 free(c);puts("Unified iOS contracts PASS");
}}
