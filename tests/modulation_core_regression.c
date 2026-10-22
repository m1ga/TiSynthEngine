#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../shared/synth_modulation.h"
int main(void) {
    SynthModulation m; synth_modulation_init(&m,48000);
    SynthModulationConfig c=synth_modulation_default_config();
    assert(!synth_modulation_is_active(&m));
    c.lfo[0].enabled=1;c.lfo[0].syncBeats=1.0/6;
    assert(synth_modulation_set_config(&m,&c,SYNTH_MOD_CAP_ALL));
    for(int i=0;i<481;i++)synth_modulation_tick(&m,300);
    assert(m.rate[0]==30);
    assert(synth_modulation_validate(&c,SYNTH_MOD_CAP_ALL));
    c.lfo[0].syncBeats=1.0/6;assert(synth_modulation_validate(&c,SYNTH_MOD_CAP_ALL));c.lfo[0].syncBeats=0;
    SynthModulationOutput out=synth_modulation_tick(&m,120);assert(out.cutoffOctaves==0&&out.pwmOffset==0&&out.fmOffset==0);
    c.lfo[0].enabled=1;c.lfo[0].target=SYNTH_MOD_TARGET_PWM;c.lfo[0].depth=1;
    assert(!synth_modulation_validate(&c,SYNTH_MOD_CAP_FILTER));
    assert(synth_modulation_set_config(&m,&c,SYNTH_MOD_CAP_ALL));
    for(int i=0;i<48000;i++){out=synth_modulation_tick(&m,120);assert(fabs(out.pwmOffset)<=.45);}
    double phase=m.phase[0];c.lfo[0].rateHz=3;assert(synth_modulation_set_config(&m,&c,SYNTH_MOD_CAP_ALL));assert(m.phase[0]==phase);
    c.lfo[0].target=SYNTH_MOD_TARGET_CUTOFF;c.lfo[0].syncBeats=.5;c.lfo[0].waveform=SYNTH_MOD_TRIANGLE;
    c.lfo[1]=c.lfo[0];assert(synth_modulation_set_config(&m,&c,SYNTH_MOD_CAP_ALL));
    for(int i=0;i<48000;i++){out=synth_modulation_tick(&m,300);assert(isfinite(out.cutoffOctaves)&&fabs(out.cutoffOctaves)<=4);}
    c.lfo[0].rateHz=NAN;assert(!synth_modulation_set_config(&m,&c,SYNTH_MOD_CAP_ALL));
    assert(synth_modulation_cutoff(10000,4,16000)<=7200);
    assert(synth_modulation_pwm(.5,1)==.95);assert(synth_modulation_fm(7,5)==10);
    c=synth_modulation_default_config();assert(synth_modulation_set_config(&m,&c,SYNTH_MOD_CAP_ALL));
    assert(synth_modulation_is_active(&m));
    for(int i=0;i<481;i++)synth_modulation_tick(&m,120);
    assert(!synth_modulation_is_active(&m));
    puts("modulation core regression passed");
}
