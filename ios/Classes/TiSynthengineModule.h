#import "TiModule.h"

@interface TiSynthengineModule : TiModule

- (NSDictionary *)getStats:(id)args;
- (NSNumber *)setStatsEnabled:(id)args;
- (void)resetStats:(id)args;
- (NSDictionary *)getDefaults:(id)args;
- (NSNumber *)start:(id)args;
- (NSNumber *)setVolume:(id)args;
- (NSNumber *)playTone:(id)args;
- (NSNumber *)noteOn:(id)args;
- (NSNumber *)noteOff:(id)args;
- (NSNumber *)updateVoice:(id)args;
- (NSNumber *)playChord:(id)args;
- (void)stopAll:(id)args;
- (void)shutdown:(id)args;

- (NSNumber *)setTempo:(id)args;
- (NSNumber *)playSequence:(id)args;
- (NSNumber *)updateSequence:(id)args;
- (NSNumber *)stopSequence:(id)args;
- (NSDictionary *)getSequenceState:(id)args;
- (NSNumber *)setMasterProcessing:(id)args;
@end
