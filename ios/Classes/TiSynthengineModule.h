#import "TiModule.h"

@interface TiSynthengineModule : TiModule

- (NSDictionary *)getDefaults:(id)args;
- (NSNumber *)startEngine:(id)args;
- (NSNumber *)setVolume:(id)args;
- (NSNumber *)playTone:(id)args;
- (NSNumber *)playChord:(id)args;
- (NSNumber *)playPattern:(id)args;
- (void)stopAll:(id)args;
- (void)shutdown:(id)args;

@end
