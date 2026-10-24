#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../shared/synth_effects.h"
int main(void) {
 SynthEffects fx;assert(synth_effects_init(&fx,48000));
 SynthEffectsConfig c=synth_effects_default_config();assert(c.applyToMask==15);
 c.saturation.enabled=1;c.saturation.drive=4;c.applyToMask=1u<<SYNTH_BUS_MUSIC;
 synth_effects_set_config(&fx,&c);
 SynthSend send;synth_send_init(&send,SYNTH_BUS_EFFECTS,-1,c.applyToMask);assert(synth_send_tick(&send)==0);
 synth_send_update(&send,SYNTH_BUS_EFFECTS,.25,c.applyToMask,480);
 for(int i=0;i<240;i++) {
  synth_send_tick(&send);
 }
 assert(fabs(send.value-.125)<1e-12);
 for(int i=0;i<240;i++) {
  synth_send_tick(&send);
 }
 assert(send.value==.25);
 double l,r;
 for(int i=0;i<500;i++)synth_effects_process_routed(&fx,120,.1,-.1,.3,.3,1,&l,&r);
 double expected=tanh(1.2)/tanh(4);
 assert(fabs(l-(expected+.1))<1e-12&&fabs(r-(expected-.1))<1e-12);
 /* Unselected source never drives saturation, but receives global input/master gain. */
 c.inputGain=2;synth_effects_set_config(&fx,&c);
 for(int i=0;i<500;i++)synth_effects_process_routed(&fx,120,.1,-.2,0,0,.5,&l,&r);
 assert(l==.1&&r==-.2);
 c.applyToMask=16;assert(!synth_effects_validate(&c));
 synth_effects_destroy(&fx);
 assert(synth_effects_init(&fx,192000));
 /* Leave room for four triple-bank timelines, 32 voices/tails and commands. */
 assert(synth_effects_memory_bytes(&fx)<20u*1024u*1024u);
 synth_effects_destroy(&fx);puts("routing: category exclusion, explicit send, 10ms ramp, selected saturation and global gain PASS");
}
