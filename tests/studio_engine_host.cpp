// Same host build flags as engine_render_host.cpp.
#define main legacy_main
#include "engine_render_host.cpp"
#undef main
#include <cassert>
static void sequence(Rig &r,int notes=1) {
 auto *c=synth_transport_begin_write(&r.e().transport_,1,1);assert(c);c->stepCount=1;c->loop=1;
 c->steps[0].noteCount=notes;c->steps[0].gate=.1;c->steps[0].voice.volume=.03;
 for(int i=0;i<notes;i++)c->steps[0].notes[i]=(uint8_t)(48+i);
 assert(synth_transport_commit(&r.e().transport_,c,1));
}
static int sequenceActive(Rig &r,bool releasing=false) {int n=0;for(auto &v:r.e().voices_)if(v.active&&v.sequenceOwned&&(!releasing||v.held.releasing))n++;return n;}
int main(){
 // A gate ending on a step must release before the new attack chooses a slot.
 // Otherwise an older keyboard note is stolen instead of the now-releasing sequence note.
 { Rig full(2);auto key=note(full,440,100,5,80,.03,0);key.type=SYNTH_CMD_HELD_ON;key.voiceId=500;
   assert(!full.e().enqueue(key));full.render(256);
   auto *config=synth_transport_begin_write(&full.e().transport_,1,1);assert(config);config->stepCount=1;config->loop=1;
   config->steps[0].noteCount=1;config->steps[0].notes[0]=60;config->steps[0].gate=1;config->steps[0].voice.volume=.03;
   assert(synth_transport_commit(&full.e().transport_,config,1));full.render(6001);
   bool retained=false;for(auto &v:full.e().voices_)if(v.active&&v.held.voiceId==500)retained=!v.held.releasing;
   assert(retained);
 }
 Rig r(32);sequence(r);r.render(512);assert(sequenceActive(r)==1);
 // Saturate the public command queue: native musical gates still execute.
 for(int i=0;i<127;i++){Command c;c.type=99;assert(!r.e().enqueue(c));}
 r.render(120);assert(sequenceActive(r,true)==1);
 auto c=note(r,440,100,5,20,.03,0);c.type=SYNTH_CMD_HELD_ON;c.voiceId=42;
 assert(!r.e().enqueue(c));r.render(512);r.e().stopSequence(1);r.render(512);
 assert(!r.e().transport_.slots[0].playing);
 bool keyboard=false;for(auto &v:r.e().voices_)if(v.active&&v.held.voiceId==42)keyboard=!v.held.releasing;
 assert(keyboard);
 // Chords consume the same bounded voice pool; stale gates cannot release replacements.
 Rig dense(32);sequence(dense,32);dense.render(512);assert(sequenceActive(dense)==32);
 c.voiceId=99;assert(!dense.e().enqueue(c));dense.render(2000);
 keyboard=false;for(auto &v:dense.e().voices_)if(v.active&&v.held.voiceId==99)keyboard=!v.held.releasing;
 assert(keyboard);
 // Public processing changes are applied in the render path before final protection.
 Rig fx(32);auto conf=synth_effects_default_config();conf.inputGain=4;conf.saturation.enabled=1;conf.saturation.drive=4;
 Command effect;effect.type=8;effect.effects=conf;assert(!fx.e().enqueue(effect));
 c.voiceId=1;c.volume=2;assert(!fx.e().enqueue(c));auto samples=fx.render(48000);assert(peak(samples)<=.850001);
 Command update;update.type=SYNTH_CMD_HELD_UPDATE;update.voiceId=1;update.hasModulations=true;
 update.modulations.lfo[0]={1,SYNTH_MOD_TRIANGLE,SYNTH_MOD_TARGET_FM,2,0,.5};
 assert(!fx.e().enqueue(update));fx.render(500);Voice *voice=nullptr;for(auto &v:fx.e().voices_)if(v.active&&v.held.voiceId==1)voice=&v;
 assert(voice&&voice->modulation.config.lfo[0].enabled);double phase=voice->modulation.phase[0];
 update.modulations.lfo[0].rateHz=4;assert(!fx.e().enqueue(update));fx.e().processCommands();assert(voice->modulation.phase[0]==phase);
 puts("studio renderer: native gates under queue saturation, shared pool, isolated stop, stale releases, effects ceiling, phase-continuous LFO update PASS");
}
