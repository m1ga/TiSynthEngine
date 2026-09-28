#import <Foundation/Foundation.h>

@interface TiSynthengineContract : NSObject

+ (NSDictionary *)validateStartEngine:(NSDictionary *)options;
+ (NSDictionary *)validateSetVolume:(NSDictionary *)options;
+ (NSDictionary *)validateTone:(NSDictionary *)options maxFrequency:(double)maxFrequency;
+ (NSDictionary *)validateChord:(NSDictionary *)options maxFrequency:(double)maxFrequency;
+ (NSDictionary *)validatePattern:(NSDictionary *)options maxFrequency:(double)maxFrequency;
+ (NSDictionary *)defaultsWithMaxFrequency:(double)maxFrequency;

@end
