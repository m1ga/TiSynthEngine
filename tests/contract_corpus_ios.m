// Replays tests/contract_corpus.jsonl (written by the Java contract regression)
// against the iOS contract so the two ports cannot drift apart. macOS only: it
// links Foundation and needs no Titanium.
//
//   clang -fobjc-arc -Wall -Wextra -framework Foundation \
//     tests/contract_corpus_ios.m ios/Classes/TiSynthengineContract.m \
//     -o /tmp/contract_corpus_ios && /tmp/contract_corpus_ios
//
// Run from the repository root.
#import <Foundation/Foundation.h>
#import "../ios/Classes/TiSynthengineContract.h"

int main(void)
{
  @autoreleasepool {
    NSError *error = nil;
    NSString *corpus = [NSString stringWithContentsOfFile:@"tests/contract_corpus.jsonl"
                                                 encoding:NSUTF8StringEncoding error:&error];
    if (!corpus) {
      fprintf(stderr, "cannot read tests/contract_corpus.jsonl: %s\n", error.localizedDescription.UTF8String);
      return 2;
    }

    NSInteger checked = 0, failures = 0;
    NSInteger lineNumber = 0;
    for (NSString *line in [corpus componentsSeparatedByString:@"\n"]) {
      lineNumber++;
      if (line.length == 0) continue;
      NSDictionary *golden = [NSJSONSerialization JSONObjectWithData:[line dataUsingEncoding:NSUTF8StringEncoding]
                                                             options:0 error:&error];
      if (![golden isKindOfClass:[NSDictionary class]]) {
        fprintf(stderr, "line %ld: invalid JSON\n", (long)lineNumber);
        return 2;
      }

      NSString *method = golden[@"method"];
      NSDictionary *options = golden[@"options"];
      double maxFrequency = [golden[@"max"] doubleValue];
      NSDictionary *failure;
      if ([method isEqualToString:@"noteOn"]) {
        failure = [TiSynthengineContract validateNoteOn:options maxFrequency:maxFrequency];
      } else if ([method isEqualToString:@"noteOff"]) {
        failure = [TiSynthengineContract validateNoteOff:options];
      } else if ([method isEqualToString:@"updateVoice"]) {
        failure = [TiSynthengineContract validateUpdateVoice:options maxFrequency:maxFrequency];
      } else if ([method isEqualToString:@"playTone"]) {
        failure = [TiSynthengineContract validateTone:options maxFrequency:maxFrequency];
      } else if ([method isEqualToString:@"playChord"]) {
        failure = [TiSynthengineContract validateChord:options maxFrequency:maxFrequency];
      } else if ([method isEqualToString:@"start"]) {
        failure = [TiSynthengineContract validateStart:options];
      } else {
        fprintf(stderr, "line %ld: unknown method %s\n", (long)lineNumber, method.UTF8String);
        return 2;
      }

      BOOL expectedAccepted = [golden[@"accepted"] boolValue];
      BOOL ok = expectedAccepted ? failure == nil
        : (failure != nil
           && [failure[@"key"] isEqualToString:golden[@"key"]]
           && [failure[@"reason"] isEqualToString:golden[@"reason"]]);
      checked++;
      if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %ld %s %s\n  expected: %s %s/%s\n  actual:   %s\n", (long)lineNumber,
                method.UTF8String, [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:options options:NSJSONWritingSortedKeys error:nil] encoding:NSUTF8StringEncoding].UTF8String,
                expectedAccepted ? "accepted" : "rejected",
                [golden[@"key"] UTF8String] ?: "", [golden[@"reason"] UTF8String] ?: "",
                failure ? [NSString stringWithFormat:@"rejected %@/%@", failure[@"key"], failure[@"reason"]].UTF8String : "accepted");
      }
    }

    if (checked == 0) {
      fprintf(stderr, "the corpus is empty\n");
      return 2;
    }
    if (failures) {
      fprintf(stderr, "contract_corpus_ios: %ld of %ld cases differ\n", (long)failures, (long)checked);
      return 1;
    }
    printf("contract_corpus_ios: all %ld cases match\n", (long)checked);
  }
  return 0;
}
