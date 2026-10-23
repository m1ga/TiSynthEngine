// Shared new-API acceptance corpus, Foundation only. See run_studio_java.py.
#import <Foundation/Foundation.h>
#import "../ios/Classes/TiSynthengineContract.h"
#include <assert.h>
int main(int argc,char **argv) { @autoreleasepool {
 NSString *source=[NSString stringWithContentsOfFile:(argc>1?[NSString stringWithUTF8String:argv[1]]:@"tests/studio_contract_corpus.jsonl") encoding:NSUTF8StringEncoding error:NULL];assert(source);
 int count=0,failures=0;
 SynthSequenceConfig *config=calloc(1,sizeof(*config));
 for(NSString *line in [source componentsSeparatedByString:@"\n"]) {
  if(!line.length)continue;count++;
  NSDictionary *row=[NSJSONSerialization JSONObjectWithData:[line dataUsingEncoding:NSUTF8StringEncoding] options:0 error:NULL];assert(row);
  id options=row[@"options"];NSDictionary *failure=nil;NSString *api=row[@"api"];
  double maximum=row[@"maxFrequency"]?[row[@"maxFrequency"] doubleValue]:20000;
  if([api isEqual:@"sequence"]) {
   NSMutableDictionary *merged=[(row[@"previous"]?:([row[@"update"] boolValue]?@{@"steps":@[@{@"rest":@YES}]}:@{})) mutableCopy];
   if([options isKindOfClass:[NSDictionary class]]) {
    [merged addEntriesFromDictionary:options];
    if(row[@"previous"]&&[options[@"voice"] isKindOfClass:[NSDictionary class]]){merged[@"voice"]=[TiSynthengineContract mergeSequenceVoice:row[@"previous"][@"voice"]?:@{} changes:options[@"voice"]];}
    failure=[TiSynthengineContract prepareSequence:merged config:config maxFrequency:maximum];
   } else failure=@{};
  } else if([api isEqual:@"masterProcessing"]) {
   SynthEffectsConfig fx=synth_effects_default_config();
   if(row[@"previous"])failure=[TiSynthengineContract prepareMasterProcessing:row[@"previous"] config:&fx];
   if(!failure)failure=[TiSynthengineContract prepareMasterProcessing:options config:&fx];
  } else if([api isEqual:@"noteOn"])failure=[TiSynthengineContract validateNoteOn:options maxFrequency:maximum];
  else if([api isEqual:@"updateVoice"])failure=[TiSynthengineContract validateUpdateVoice:options maxFrequency:maximum];
  else abort();
  if((failure==nil)!=[row[@"accepted"] boolValue]){fprintf(stderr,"Studio parity case %d %s expected %d: %s\n",count,api.UTF8String,[row[@"accepted"] boolValue],failure.description.UTF8String);failures++;}
 }
 free(config);printf("Studio iOS corpus: %d cases, %d failures\n",count,failures);return failures?1:0;
}}
